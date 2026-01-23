#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "wifi.h"
#include "mqtt.h"
#include "bt_presence.h"
#include "provisioning.h"

#define BOOT_BUTTON_GPIO    0
#define FACTORY_RESET_HOLD_MS  5000

static const char *TAG = "MAIN";

static QueueHandle_t s_scan_queue = NULL;
static TickType_t s_last_scan_time = 0;
static TickType_t s_button_press_start = 0;
static bool s_button_was_pressed = false;

// Initialize BOOT button for factory reset
static void init_boot_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "BOOT button (GPIO%d) initialized for factory reset", BOOT_BUTTON_GPIO);
}

// Check if BOOT button is held for factory reset
// Returns true if factory reset was triggered
static bool check_factory_reset_button(void)
{
    bool button_pressed = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);  // Active low

    if (button_pressed && !s_button_was_pressed) {
        // Button just pressed
        s_button_press_start = xTaskGetTickCount();
        s_button_was_pressed = true;
        ESP_LOGI(TAG, "BOOT button pressed - hold 5s for factory reset");
    } else if (button_pressed && s_button_was_pressed) {
        // Button still held
        TickType_t held_time = xTaskGetTickCount() - s_button_press_start;
        if (held_time >= pdMS_TO_TICKS(FACTORY_RESET_HOLD_MS)) {
            ESP_LOGW(TAG, "*** FACTORY RESET TRIGGERED ***");
            prov_factory_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return true;  // Never reached
        }
    } else if (!button_pressed && s_button_was_pressed) {
        // Button released
        s_button_was_pressed = false;
        ESP_LOGI(TAG, "BOOT button released");
    }

    return false;
}

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
            char device_name[32];
            bt_get_device_name(device_name);
            ESP_LOGI(TAG, "*** PAIRING MODE ENABLED ***");
            ESP_LOGI(TAG, "ESP32 is now discoverable as '%s'", device_name);
            ESP_LOGI(TAG, "Go to phone Bluetooth settings and pair with '%s'", device_name);
            bt_presence_enable_pairing();
            s_pairing_mode = true;
            mqtt_publish_pairing_status(true);
        } else if (strcmp(mac, "off") == 0 && s_pairing_mode) {
            ESP_LOGI(TAG, "Pairing mode disabled");
            bt_presence_disable_pairing();
            s_pairing_mode = false;
            mqtt_publish_pairing_status(false);
        }
    } else if (strcmp(action, "reprovision") == 0) {
        ESP_LOGW(TAG, "Reprovisioning requested - clearing WiFi/MQTT config and restarting...");
        prov_clear_config();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else if (strcmp(action, "factory_reset") == 0) {
        ESP_LOGW(TAG, "Factory reset requested - clearing ALL config and restarting...");
        prov_factory_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
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

// Check if Kconfig has placeholder/empty WiFi config
static bool kconfig_has_valid_wifi(void)
{
    // If SSID is empty or a placeholder, consider it invalid
    const char *ssid = CONFIG_WIFI_SSID;
    if (strlen(ssid) == 0) return false;
    if (strcmp(ssid, "your_ssid") == 0) return false;
    if (strcmp(ssid, "YOUR_WIFI_SSID") == 0) return false;
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 Bluetooth Presence Detector");
    ESP_LOGI(TAG, "========================================");

    // Initialize BOOT button for factory reset
    init_boot_button();

    // Check if BOOT button is pressed at startup for factory reset
    if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        ESP_LOGW(TAG, "BOOT button pressed at startup!");
        ESP_LOGW(TAG, "Hold for 5 seconds for factory reset...");

        int held_count = 0;
        while (gpio_get_level(BOOT_BUTTON_GPIO) == 0 && held_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            held_count++;
            if (held_count % 10 == 0) {
                ESP_LOGI(TAG, "  %d seconds...", held_count / 10);
            }
        }

        if (held_count >= 50) {
            ESP_LOGW(TAG, "*** FACTORY RESET TRIGGERED ***");
            // Initialize NVS first so we can erase it
            nvs_flash_init();
            prov_factory_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGI(TAG, "Button released - continuing normal boot");
        }
    }

    // Initialize NVS first (needed for provisioning check)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Check if we need to enter provisioning mode
    // Provisioning is needed if:
    // 1. Force flag is set (from reprovision command) OR
    // 2. Device is not provisioned AND Kconfig doesn't have valid WiFi credentials
    bool need_provisioning = false;

    if (prov_is_forced()) {
        ESP_LOGW(TAG, "Forced provisioning requested!");
        // NOTE: Don't clear flag here - it will be cleared after successful provisioning
        need_provisioning = true;
    } else if (!prov_is_provisioned() && !kconfig_has_valid_wifi()) {
        ESP_LOGW(TAG, "No WiFi configuration found!");
        need_provisioning = true;
    }

    if (need_provisioning) {
        ESP_LOGI(TAG, "Starting provisioning mode...");
        // We need some identifier for the AP name
        // Use a fixed suffix for now (will get real MAC after BT init)
        prov_start("Setup");
        // prov_start never returns - it restarts the device
    }

    s_scan_queue = xQueueCreate(5, sizeof(uint8_t));
    if (s_scan_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create scan queue");
        return;
    }

    // Initialize WiFi (will use provisioned or Kconfig config)
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        ESP_LOGW(TAG, "Running in offline mode - hold BOOT 5s for factory reset");
        // Don't exit - continue to main loop so button check works
    }

    // Initialize Bluetooth
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    ret = bt_presence_init(on_presence_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed!");
        ESP_LOGW(TAG, "Hold BOOT 5s for factory reset");
        // Continue to main loop for button check
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
        ESP_LOGW(TAG, "Hold BOOT 5s for factory reset");
        // Continue to main loop for button check
    }

    // Wait for MQTT connection (with button check)
    ESP_LOGI(TAG, "Waiting for MQTT connection...");
    int mqtt_wait = 0;
    while (!mqtt_is_connected() && mqtt_wait < 30) {
        // Check button 10 times per second during wait
        for (int i = 0; i < 10; i++) {
            check_factory_reset_button();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        mqtt_wait++;
    }

    if (!mqtt_is_connected()) {
        ESP_LOGW(TAG, "MQTT connection failed - running in offline mode");
        ESP_LOGI(TAG, "Hold BOOT button 5s for factory reset");
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

    // Get the unique Bluetooth device name
    char bt_device_name[32];
    bt_get_device_name(bt_device_name);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System Ready!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Bluetooth name: %s", bt_device_name);
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
    ESP_LOGI(TAG, "  SYSTEM:");
    ESP_LOGI(TAG, "    .../system/reprovision   - Reset WiFi/MQTT config");
    ESP_LOGI(TAG, "    .../system/factory_reset - Reset ALL config (full wipe)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  FACTORY RESET: Hold BOOT button for 5 seconds");
    ESP_LOGI(TAG, "Configured devices: %d", bt_presence_get_device_count());
    ESP_LOGI(TAG, "");

    while (1) {
        // Check for factory reset button (every 100ms for responsiveness)
        for (int i = 0; i < 100; i++) {
            check_factory_reset_button();
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "Status: WiFi=%s, MQTT=%s, Devices=%d",
                 wifi_is_connected() ? "OK" : "DISCONNECTED",
                 mqtt_is_connected() ? "OK" : "DISCONNECTED",
                 bt_presence_get_device_count());
    }
#endif
}
