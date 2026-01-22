#include "mqtt.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_connected = false;
static mqtt_scan_request_cb_t s_scan_callback = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker");
            s_is_connected = true;
            // Subscribe to scan request topic
            esp_mqtt_client_subscribe(s_mqtt_client, CONFIG_MQTT_TOPIC_SCAN_REQUEST, 1);
            ESP_LOGI(TAG, "Subscribed to: %s", CONFIG_MQTT_TOPIC_SCAN_REQUEST);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            s_is_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received message on topic: %.*s", event->topic_len, event->topic);
            // Check if it's a scan request
            if (strncmp(event->topic, CONFIG_MQTT_TOPIC_SCAN_REQUEST, event->topic_len) == 0) {
                ESP_LOGI(TAG, "Scan request received!");
                if (s_scan_callback) {
                    s_scan_callback();
                }
            }
            break;

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

esp_err_t mqtt_init(mqtt_scan_request_cb_t scan_cb)
{
    s_scan_callback = scan_cb;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
        .credentials.username = CONFIG_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_PASSWORD,
    };

    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", CONFIG_MQTT_BROKER_URI);

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
    char payload[256];

    // Get timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long timestamp = tv.tv_sec;

    snprintf(topic, sizeof(topic), "%s/%s/state", CONFIG_MQTT_TOPIC_PRESENCE_BASE, device_name);
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\",\"ts\":%ld,\"reason\":\"%s\"}",
             present ? "present" : "absent",
             timestamp,
             reason);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published to %s: %s", topic, payload);
    return ESP_OK;
}

esp_err_t mqtt_publish_scan_status(const char *status)
{
    if (!s_is_connected || !s_mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/scan/status", CONFIG_MQTT_TOPIC_PRESENCE_BASE);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, status, 0, 1, 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Scan status: %s", status);
    return ESP_OK;
}

bool mqtt_is_connected(void)
{
    return s_is_connected;
}
