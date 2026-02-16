#include "mqtt.h"
#include "provisioning.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_connected = false;
static mqtt_scan_request_cb_t s_scan_callback = NULL;
static mqtt_config_cb_t s_config_callback = NULL;
static char s_esp32_mac[18] = {0};  // MAC address string "XX:XX:XX:XX:XX:XX"
static char s_topic_prefix[80] = {0};  // "<topic_base>/XXXXXXXXXXXX" (64 + 1 + 12 + 1)

// Topic suffixes
static char s_topic_scan_request[128] = {0};
static char s_topic_config_add[128] = {0};
static char s_topic_config_remove[128] = {0};
static char s_topic_config_list[128] = {0};
static char s_topic_pairing_set[128] = {0};
static char s_topic_reprovision[128] = {0};
static char s_topic_factory_reset[128] = {0};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker");
            s_is_connected = true;

            // Subscribe to scan request topic
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_scan_request, 1);
            ESP_LOGI(TAG, "Subscribed to: %s", s_topic_scan_request);

            // Subscribe to config topics
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_config_add, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_config_remove, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_config_list, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_pairing_set, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_reprovision, 1);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_factory_reset, 1);
            ESP_LOGI(TAG, "Subscribed to config topics");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            s_is_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            // Null-terminate topic and data for string operations
            char topic[128] = {0};
            char data[256] = {0};

            int topic_len = event->topic_len < sizeof(topic) - 1 ? event->topic_len : sizeof(topic) - 1;
            int data_len = event->data_len < sizeof(data) - 1 ? event->data_len : sizeof(data) - 1;

            strncpy(topic, event->topic, topic_len);
            strncpy(data, event->data, data_len);

            ESP_LOGI(TAG, "Received: %s -> %s", topic, data);

            // Check if it's a scan request
            if (strcmp(topic, s_topic_scan_request) == 0) {
                ESP_LOGI(TAG, "Scan request received!");
                if (s_scan_callback) {
                    s_scan_callback();
                }
            }
            // Check if it's a config add command
            else if (strcmp(topic, s_topic_config_add) == 0) {
                // Expected format: "MAC,name" e.g., "AA:BB:CC:DD:EE:FF,phone_name"
                char *comma = strchr(data, ',');
                if (comma && s_config_callback) {
                    *comma = '\0';
                    char *mac = data;
                    char *name = comma + 1;
                    ESP_LOGI(TAG, "Config ADD: MAC=%s, Name=%s", mac, name);
                    s_config_callback("add", mac, name);
                }
            }
            // Check if it's a config remove command
            else if (strcmp(topic, s_topic_config_remove) == 0) {
                // Expected format: "MAC" or "name"
                ESP_LOGI(TAG, "Config REMOVE: %s", data);
                if (s_config_callback) {
                    s_config_callback("remove", data, data);
                }
            }
            // Check if it's a config list request
            else if (strcmp(topic, s_topic_config_list) == 0) {
                ESP_LOGI(TAG, "Config LIST request");
                if (s_config_callback) {
                    s_config_callback("list", "", "");
                }
            }
            // Check if it's a pairing mode command
            else if (strcmp(topic, s_topic_pairing_set) == 0) {
                // Expected format: "on" or "off"
                ESP_LOGI(TAG, "Pairing mode: %s", data);
                if (s_config_callback) {
                    s_config_callback("pairing", data, "");
                }
            }
            // Check if it's a reprovision command
            else if (strcmp(topic, s_topic_reprovision) == 0) {
                ESP_LOGW(TAG, "Reprovision command received!");
                if (s_config_callback) {
                    s_config_callback("reprovision", "", "");
                }
            }
            // Check if it's a factory reset command
            else if (strcmp(topic, s_topic_factory_reset) == 0) {
                ESP_LOGW(TAG, "Factory reset command received!");
                if (s_config_callback) {
                    s_config_callback("factory_reset", "", "");
                }
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: %s", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGD(TAG, "Other MQTT event id: %d", event->event_id);
            break;
    }
}

esp_err_t mqtt_init(mqtt_scan_request_cb_t scan_cb, mqtt_config_cb_t config_cb, const char *esp32_mac)
{
    s_scan_callback = scan_cb;
    s_config_callback = config_cb;

    // Store ESP32 MAC (convert to format without colons for topic)
    strncpy(s_esp32_mac, esp32_mac, sizeof(s_esp32_mac) - 1);

    // Create MAC without colons for topic
    char mac_no_colons[13] = {0};
    int j = 0;
    for (int i = 0; esp32_mac[i] && j < 12; i++) {
        if (esp32_mac[i] != ':') {
            mac_no_colons[j++] = esp32_mac[i];
        }
    }

    // Load MQTT config - check provisioning first, then fallback to Kconfig
    esp_mqtt_client_config_t mqtt_cfg = {0};
    const char *topic_base = CONFIG_MQTT_TOPIC_PRESENCE_BASE;  // default

    prov_config_t prov_config;
    const char *broker_uri = NULL;

    if (prov_is_provisioned() && prov_load_config(&prov_config) == ESP_OK) {
        ESP_LOGI(TAG, "Using provisioned MQTT config");
        broker_uri = prov_config.mqtt_uri;
        mqtt_cfg.broker.address.uri = prov_config.mqtt_uri;
        mqtt_cfg.credentials.username = prov_config.mqtt_username;
        mqtt_cfg.credentials.authentication.password = prov_config.mqtt_password;
        topic_base = prov_config.mqtt_topic_base;
    } else {
        ESP_LOGI(TAG, "Using Kconfig default MQTT config");
        broker_uri = CONFIG_MQTT_BROKER_URI;
        mqtt_cfg.broker.address.uri = CONFIG_MQTT_BROKER_URI;
        mqtt_cfg.credentials.username = CONFIG_MQTT_USERNAME;
        mqtt_cfg.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
    }

    // Auto-detect TLS from URI prefix (mqtts://)
    if (strncmp(broker_uri, "mqtts://", 8) == 0) {
        ESP_LOGI(TAG, "TLS enabled (mqtts:// detected)");
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        ESP_LOGI(TAG, "TLS disabled (plain mqtt://)");
    }

    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", broker_uri);

    // Build topic prefix: <topic_base>/XXXXXXXXXXXX
    snprintf(s_topic_prefix, sizeof(s_topic_prefix), "%s/%s", topic_base, mac_no_colons);

    // Build specific topics
    snprintf(s_topic_scan_request, sizeof(s_topic_scan_request), "%s/scan/request", s_topic_prefix);
    snprintf(s_topic_config_add, sizeof(s_topic_config_add), "%s/config/add", s_topic_prefix);
    snprintf(s_topic_config_remove, sizeof(s_topic_config_remove), "%s/config/remove", s_topic_prefix);
    snprintf(s_topic_config_list, sizeof(s_topic_config_list), "%s/config/list", s_topic_prefix);
    snprintf(s_topic_pairing_set, sizeof(s_topic_pairing_set), "%s/pairing/set", s_topic_prefix);
    snprintf(s_topic_reprovision, sizeof(s_topic_reprovision), "%s/system/reprovision", s_topic_prefix);
    snprintf(s_topic_factory_reset, sizeof(s_topic_factory_reset), "%s/system/factory_reset", s_topic_prefix);

    ESP_LOGI(TAG, "Topic prefix: %s", s_topic_prefix);

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));

    return ESP_OK;
}

esp_err_t mqtt_publish_presence(const char *device_name, bool present, const char *reason)
{
    if (!s_is_connected || !s_mqtt_client) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    const char *payload = present ? "home" : "not_home";

    // Topic: home/presence/<ESP32_MAC>/<device_name>/state
    snprintf(topic, sizeof(topic), "%s/%s/state", s_topic_prefix, device_name);

    // Publish with retain so Home Assistant gets last known state
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 1);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s: %s (reason: %s)", topic, payload, reason);
    return ESP_OK;
}

esp_err_t mqtt_publish_scan_status(const char *status)
{
    if (!s_is_connected || !s_mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/scan/status", s_topic_prefix);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, status, 0, 1, 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Scan status: %s", status);
    return ESP_OK;
}

esp_err_t mqtt_publish_config(const char *devices_json)
{
    if (!s_is_connected || !s_mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/config/devices", s_topic_prefix);

    // Publish with retain so config persists
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, devices_json, 0, 1, 1);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published config: %s", devices_json);
    return ESP_OK;
}

bool mqtt_is_connected(void)
{
    return s_is_connected;
}

esp_err_t mqtt_publish_pairing_status(bool enabled)
{
    if (!s_is_connected || !s_mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/pairing/status", s_topic_prefix);

    const char *payload = enabled ? "on" : "off";
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 1);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Pairing status: %s", payload);
    return ESP_OK;
}
