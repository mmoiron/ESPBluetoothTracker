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
    // Check minimum scan interval
    TickType_t now = xTaskGetTickCount();
    if ((now - s_last_scan_time) < pdMS_TO_TICKS(CONFIG_BT_MIN_SCAN_INTERVAL_MS)) {
        ESP_LOGW(TAG, "Scan request ignored - too soon after last scan");
        return;
    }

    // Queue scan request
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
             device->present ? "PRESENT" : "ABSENT",
             device->reason);

    // Publish to MQTT
    mqtt_publish_presence(device->name, device->present, device->reason);
}

// Task to handle presence scanning
static void scan_task(void *pvParameters)
{
    uint8_t msg;

    while (1) {
        // Wait for scan request
        if (xQueueReceive(s_scan_queue, &msg, portMAX_DELAY) == pdTRUE) {
            s_last_scan_time = xTaskGetTickCount();

            // Publish scan started status
            mqtt_publish_scan_status("started");

            // Perform the scan
            bt_presence_scan();

            // Publish scan completed status
            mqtt_publish_scan_status("completed");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 Bluetooth Presence Detector");
    ESP_LOGI(TAG, "========================================");

    // Create scan queue
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

    // Check if pairing mode is enabled
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
    ESP_LOGI(TAG, "After pairing, note the phone's Bluetooth MAC address");
    ESP_LOGI(TAG, "from the serial output or your phone's settings.");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Then disable pairing mode in menuconfig and");
    ESP_LOGI(TAG, "configure the MAC addresses.");
    ESP_LOGI(TAG, "");

    bt_presence_enable_pairing();

    // In pairing mode, just wait and show discovered devices
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    // Normal operation mode

    // Initialize MQTT
    ESP_LOGI(TAG, "Initializing MQTT...");
    ret = mqtt_init(on_scan_request);
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

    // Create scan task
    xTaskCreate(scan_task, "scan_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System Ready!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Subscribed to: %s", CONFIG_MQTT_TOPIC_SCAN_REQUEST);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "To trigger a scan, publish to:");
    ESP_LOGI(TAG, "  mosquitto_pub -h %s -u %s -P %s -t %s -m \"{}\"",
             "192.168.1.92", CONFIG_MQTT_USERNAME, CONFIG_MQTT_PASSWORD,
             CONFIG_MQTT_TOPIC_SCAN_REQUEST);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "To see results:");
    ESP_LOGI(TAG, "  mosquitto_sub -h %s -u %s -P %s -t \"%s/#\" -v",
             "192.168.1.92", CONFIG_MQTT_USERNAME, CONFIG_MQTT_PASSWORD,
             CONFIG_MQTT_TOPIC_PRESENCE_BASE);
    ESP_LOGI(TAG, "");

    // Main loop - just keep the task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        // Periodic status
        ESP_LOGI(TAG, "Status: WiFi=%s, MQTT=%s",
                 wifi_is_connected() ? "OK" : "DISCONNECTED",
                 mqtt_is_connected() ? "OK" : "DISCONNECTED");
    }
#endif
}
