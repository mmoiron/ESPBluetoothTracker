#include "bt_presence.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "BT_PRESENCE";

#define MAX_DEVICES 2

static bt_device_t s_devices[MAX_DEVICES];
static int s_num_devices = 0;
static int s_current_device_idx = -1;
static bt_presence_result_cb_t s_result_callback = NULL;
static SemaphoreHandle_t s_scan_semaphore = NULL;
static bool s_scan_in_progress = false;

// Forward declarations
static void bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void probe_device(int device_idx);

esp_err_t bt_parse_mac(const char *mac_str, uint8_t *mac_bytes)
{
    if (mac_str == NULL || mac_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int values[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 6; i++) {
        mac_bytes[i] = (uint8_t)values[i];
    }

    return ESP_OK;
}

static bool is_valid_mac(const uint8_t *mac)
{
    // Check if MAC is all zeros
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return true;
        }
    }
    return false;
}

static void bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            // Device discovered during inquiry (pairing mode)
            ESP_LOGI(TAG, "Device discovered: %02x:%02x:%02x:%02x:%02x:%02x",
                     param->disc_res.bda[0], param->disc_res.bda[1],
                     param->disc_res.bda[2], param->disc_res.bda[3],
                     param->disc_res.bda[4], param->disc_res.bda[5]);

            // Print device name if available
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    char name[64] = {0};
                    memcpy(name, param->disc_res.prop[i].val, param->disc_res.prop[i].len);
                    ESP_LOGI(TAG, "  Name: %s", name);
                }
            }
            break;

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Discovery stopped");
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started");
            }
            break;

        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
            // Name request result - lightweight presence detection
            if (s_current_device_idx >= 0 && s_current_device_idx < s_num_devices) {
                bt_device_t *dev = &s_devices[s_current_device_idx];

                if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
                    ESP_LOGI(TAG, "Device %s: Name request success, name='%s'",
                             dev->name, param->read_rmt_name.rmt_name);
                    dev->present = true;
                    strncpy(dev->reason, "name_ok", sizeof(dev->reason) - 1);
                } else {
                    ESP_LOGW(TAG, "Device %s: Name request failed (status=%d)",
                             dev->name, param->read_rmt_name.stat);
                    dev->present = false;
                    strncpy(dev->reason, "name_failed", sizeof(dev->reason) - 1);
                }

                // Notify callback
                if (s_result_callback) {
                    s_result_callback(dev);
                }

                // Signal completion
                if (s_scan_semaphore) {
                    xSemaphoreGive(s_scan_semaphore);
                }
            }
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success with: %02x:%02x:%02x:%02x:%02x:%02x",
                         param->auth_cmpl.bda[0], param->auth_cmpl.bda[1],
                         param->auth_cmpl.bda[2], param->auth_cmpl.bda[3],
                         param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);
                ESP_LOGI(TAG, "Device name: %s", param->auth_cmpl.device_name);
            } else {
                ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            ESP_LOGI(TAG, "PIN request - using default '0000'");
            esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(TAG, "Confirm request for pairing, auto-confirming");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(TAG, "Passkey notification: %06lu", param->key_notif.passkey);
            break;

        case ESP_BT_GAP_KEY_REQ_EVT:
            ESP_LOGI(TAG, "Passkey request");
            break;

        default:
            ESP_LOGD(TAG, "GAP event: %d", event);
            break;
    }
}

esp_err_t bt_presence_init(bt_presence_result_cb_t result_cb)
{
    esp_err_t ret;

    s_result_callback = result_cb;
    s_scan_semaphore = xSemaphoreCreateBinary();
    if (s_scan_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Release memory for BLE (we only use Classic)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GAP callback
    ret = esp_bt_gap_register_callback(bt_gap_callback);
    if (ret) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set device name
    esp_bt_dev_set_device_name("ESP32_Presence");

    // Set SSP (Secure Simple Pairing) mode
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE; // No input/output for auto-pairing
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // Configure devices from Kconfig
    s_num_devices = 0;

    // Device 1
    if (bt_parse_mac(CONFIG_BT_DEVICE1_MAC, s_devices[0].mac) == ESP_OK &&
        is_valid_mac(s_devices[0].mac)) {
        strncpy(s_devices[0].name, CONFIG_BT_DEVICE1_NAME, sizeof(s_devices[0].name) - 1);
        s_devices[0].present = false;
        s_num_devices++;
        ESP_LOGI(TAG, "Device 1 configured: %s (%02x:%02x:%02x:%02x:%02x:%02x)",
                 s_devices[0].name,
                 s_devices[0].mac[0], s_devices[0].mac[1], s_devices[0].mac[2],
                 s_devices[0].mac[3], s_devices[0].mac[4], s_devices[0].mac[5]);
    }

    // Device 2
    if (bt_parse_mac(CONFIG_BT_DEVICE2_MAC, s_devices[1].mac) == ESP_OK &&
        is_valid_mac(s_devices[1].mac)) {
        strncpy(s_devices[1].name, CONFIG_BT_DEVICE2_NAME, sizeof(s_devices[1].name) - 1);
        s_devices[1].present = false;
        s_num_devices++;
        ESP_LOGI(TAG, "Device 2 configured: %s (%02x:%02x:%02x:%02x:%02x:%02x)",
                 s_devices[1].name,
                 s_devices[1].mac[0], s_devices[1].mac[1], s_devices[1].mac[2],
                 s_devices[1].mac[3], s_devices[1].mac[4], s_devices[1].mac[5]);
    }

    if (s_num_devices == 0) {
        ESP_LOGW(TAG, "No valid devices configured! Use menuconfig to set MAC addresses.");
    }

    ESP_LOGI(TAG, "Bluetooth initialized with %d device(s)", s_num_devices);
    return ESP_OK;
}

static void probe_device(int device_idx)
{
    if (device_idx < 0 || device_idx >= s_num_devices) {
        return;
    }

    bt_device_t *dev = &s_devices[device_idx];
    s_current_device_idx = device_idx;

    ESP_LOGI(TAG, "Probing device %s (%02x:%02x:%02x:%02x:%02x:%02x)...",
             dev->name,
             dev->mac[0], dev->mac[1], dev->mac[2],
             dev->mac[3], dev->mac[4], dev->mac[5]);

    // Clear semaphore before starting
    xSemaphoreTake(s_scan_semaphore, 0);

    // Start Name Request (lighter than SDP, works better with phones in standby)
    esp_err_t ret = esp_bt_gap_read_remote_name((uint8_t *)dev->mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start name request: %s", esp_err_to_name(ret));
        dev->present = false;
        strncpy(dev->reason, "name_error", sizeof(dev->reason) - 1);
        if (s_result_callback) {
            s_result_callback(dev);
        }
        return;
    }

    // Wait for result with timeout
    if (xSemaphoreTake(s_scan_semaphore, pdMS_TO_TICKS(CONFIG_BT_PROBE_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Device %s: Name request timeout", dev->name);
        dev->present = false;
        strncpy(dev->reason, "name_timeout", sizeof(dev->reason) - 1);
        if (s_result_callback) {
            s_result_callback(dev);
        }
    }
}

esp_err_t bt_presence_scan(void)
{
    if (s_scan_in_progress) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_num_devices == 0) {
        ESP_LOGW(TAG, "No devices configured for scanning");
        return ESP_ERR_INVALID_STATE;
    }

    s_scan_in_progress = true;
    ESP_LOGI(TAG, "Starting presence scan for %d device(s)...", s_num_devices);

    for (int i = 0; i < s_num_devices; i++) {
        probe_device(i);

        // Delay between devices
        if (i < s_num_devices - 1) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_BT_INTER_DEVICE_DELAY_MS));
        }
    }

    s_scan_in_progress = false;
    s_current_device_idx = -1;
    ESP_LOGI(TAG, "Presence scan complete");

    return ESP_OK;
}

esp_err_t bt_presence_enable_pairing(void)
{
    ESP_LOGI(TAG, "Enabling pairing mode...");

    // Make device discoverable and connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "ESP32 is now discoverable as 'ESP32_Presence'");
    ESP_LOGI(TAG, "Open Bluetooth settings on your phone and pair with this device");

    // Start discovery to show nearby devices
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);

    return ESP_OK;
}

esp_err_t bt_presence_disable_pairing(void)
{
    ESP_LOGI(TAG, "Disabling pairing mode...");

    // Stop discovery
    esp_bt_gap_cancel_discovery();

    // Make device non-discoverable but still connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

    return ESP_OK;
}
