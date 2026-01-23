#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"

#include "wifi.h"
#include "mqtt.h"
#include "bt_presence.h"

static const char *TAG = "MAIN";

static QueueHandle_t s_scan_queue = NULL;
static TickType_t s_last_scan_time = 0;

// Callback when MQTT scan request is received
static void on_scan_request(void)
{
    TickType_t now = xTaskGetTickCount();
    if ((now - s_last_scan_time) < pdMS_TO_TICKS(CONFIG_BT_MIN_SCAN_INTERVAL_MS)) {
        ESP_LOGW(TAG, "Scan request ignored - too soon after last scan");
        return;
    }

    uint8_t msg = 1;
    if (xQueueSend(s_scan_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Scan queue full");
    }
}

// Callback when presence result is available
static void on_presence_result(const bt_device_t *device)
{
    ESP_LOGI(TAG, "Presence result: %s is %s (%s)",
             device->name,
             device->present ? "HOME" : "NOT_HOME",
             device->reason);

    mqtt_publish_presence(device->name, device->present, device->reason);
}

// Track pairing mode state
static bool s_pairing_mode = false;

// Callback for device configuration changes via MQTT
static void on_config_change(const char *action, const char *mac, const char *name)
{
    ESP_LOGI(TAG, "Config change: action=%s, mac=%s, name=%s", action, mac, name);

    if (strcmp(action, "add") == 0) {
        if (bt_presence_add_device(mac, name) == ESP_OK) {
            bt_presence_save_config();
            // Publish updated config
            char config_json[512];
            bt_presence_get_config_json(config_json, sizeof(config_json));
            mqtt_publish_config(config_json);
            ESP_LOGI(TAG, "Device added and config saved");
        }
    } else if (strcmp(action, "remove") == 0) {
        if (bt_presence_remove_device(mac) == ESP_OK) {
            bt_presence_save_config();
            // Publish updated config
            char config_json[512];
            bt_presence_get_config_json(config_json, sizeof(config_json));
            mqtt_publish_config(config_json);
            ESP_LOGI(TAG, "Device removed and config saved");
        }
    } else if (strcmp(action, "list") == 0) {
        // Republish current config
        char config_json[512];
        bt_presence_get_config_json(config_json, sizeof(config_json));
        mqtt_publish_config(config_json);
        ESP_LOGI(TAG, "Config list published");
    } else if (strcmp(action, "pairing") == 0) {
        if (strcmp(mac, "on") == 0 && !s_pairing_mode) {
            ESP_LOGI(TAG, "*** PAIRING MODE ENABLED ***");
            ESP_LOGI(TAG, "ESP32 is now discoverable as 'ESP32_Presence'");
            ESP_LOGI(TAG, "Go to phone Bluetooth settings and pair with 'ESP32_Presence'");
            bt_presence_enable_pairing();
            s_pairing_mode = true;
            mqtt_publish_pairing_status(true);
        } else if (strcmp(mac, "off") == 0 && s_pairing_mode) {
            ESP_LOGI(TAG, "Pairing mode disabled");
            bt_presence_disable_pairing();
            s_pairing_mode = false;
            mqtt_publish_pairing_status(false);
        }
    }
}

// Task to handle presence scanning
static void scan_task(void *pvParameters)
{
    uint8_t msg;

    while (1) {
        if (xQueueReceive(s_scan_queue, &msg, portMAX_DELAY) == pdTRUE) {
            s_last_scan_time = xTaskGetTickCount();
            mqtt_publish_scan_status("started");
            bt_presence_scan();
            mqtt_publish_scan_status("completed");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 Bluetooth Presence Detector");
    ESP_LOGI(TAG, "========================================");

    s_scan_queue = xQueueCreate(5, sizeof(uint8_t));
    if (s_scan_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create scan queue");
        return;
    }

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        return;
    }

    // Initialize Bluetooth
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    ret = bt_presence_init(on_presence_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed!");
        return;
    }

    // Get ESP32 MAC address for MQTT topics
    char esp32_mac[18];
    bt_get_local_mac(esp32_mac);
    ESP_LOGI(TAG, "ESP32 MAC: %s", esp32_mac);

#if CONFIG_BT_PAIRING_MODE
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "*** PAIRING MODE ENABLED ***");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "The ESP32 is now discoverable.");
    ESP_LOGI(TAG, "On your phone:");
    ESP_LOGI(TAG, "  1. Go to Bluetooth settings");
    ESP_LOGI(TAG, "  2. Look for 'ESP32_Presence'");
    ESP_LOGI(TAG, "  3. Tap to pair");
    ESP_LOGI(TAG, "");

    bt_presence_enable_pairing();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    // Initialize MQTT with ESP32 MAC for unique topics
    ESP_LOGI(TAG, "Initializing MQTT...");
    ret = mqtt_init(on_scan_request, on_config_change, esp32_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT initialization failed!");
        return;
    }

    // Wait for MQTT connection
    ESP_LOGI(TAG, "Waiting for MQTT connection...");
    int mqtt_wait = 0;
    while (!mqtt_is_connected() && mqtt_wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        mqtt_wait++;
    }

    if (!mqtt_is_connected()) {
        ESP_LOGE(TAG, "MQTT connection timeout!");
        return;
    }

    // Publish current device configuration
    char config_json[512];
    bt_presence_get_config_json(config_json, sizeof(config_json));
    mqtt_publish_config(config_json);

    // Publish initial pairing status (off)
    mqtt_publish_pairing_status(false);

    // Create scan task
    xTaskCreate(scan_task, "scan_task", 4096, NULL, 5, NULL);

    // Create MAC without colons for display
    char mac_no_colons[13] = {0};
    int j = 0;
    for (int i = 0; esp32_mac[i] && j < 12; i++) {
        if (esp32_mac[i] != ':') {
            mac_no_colons[j++] = esp32_mac[i];
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System Ready!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MQTT Topics (prefix: home/presence/%s):", mac_no_colons);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  SCANNING:");
    ESP_LOGI(TAG, "    .../scan/request     - Trigger scan (any payload)");
    ESP_LOGI(TAG, "    .../scan/status      - Scan status (started/completed)");
    ESP_LOGI(TAG, "    .../<name>/state     - Presence (home/not_home, retained)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  CONFIGURATION:");
    ESP_LOGI(TAG, "    .../config/add       - Add device (payload: MAC,name)");
    ESP_LOGI(TAG, "    .../config/remove    - Remove device (payload: MAC or name)");
    ESP_LOGI(TAG, "    .../config/list      - Request device list (any payload)");
    ESP_LOGI(TAG, "    .../config/devices   - Device list (retained JSON)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  PAIRING:");
    ESP_LOGI(TAG, "    .../pairing/set      - Enable/disable (payload: on/off)");
    ESP_LOGI(TAG, "    .../pairing/status   - Current status (on/off, retained)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Configured devices: %d", bt_presence_get_device_count());
    ESP_LOGI(TAG, "");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Status: WiFi=%s, MQTT=%s, Devices=%d",
                 wifi_is_connected() ? "OK" : "DISCONNECTED",
                 mqtt_is_connected() ? "OK" : "DISCONNECTED",
                 bt_presence_get_device_count());
    }
#endif
}
