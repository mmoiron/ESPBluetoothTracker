#ifndef BT_PRESENCE_H
#define BT_PRESENCE_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_DEVICES 3

/**
 * @brief Bluetooth device info structure
 */
typedef struct {
    uint8_t mac[6];
    char name[32];
    bool present;
    char reason[32];
    bool configured;  // true if this slot has a device configured
} bt_device_t;

/**
 * @brief Callback type for presence results
 */
typedef void (*bt_presence_result_cb_t)(const bt_device_t *device);

/**
 * @brief Initialize Bluetooth for presence detection
 *
 * @param result_cb Callback function for presence results
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_init(bt_presence_result_cb_t result_cb);

/**
 * @brief Start presence scan for configured devices
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_scan(void);

/**
 * @brief Enable pairing mode (discoverable + connectable)
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_enable_pairing(void);

/**
 * @brief Disable pairing mode
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_disable_pairing(void);

/**
 * @brief Parse MAC address string to bytes
 *
 * @param mac_str MAC string in format "AA:BB:CC:DD:EE:FF"
 * @param mac_bytes Output array of 6 bytes
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_parse_mac(const char *mac_str, uint8_t *mac_bytes);

/**
 * @brief Get the ESP32's Bluetooth MAC address
 *
 * @param mac_str Output buffer for MAC string (at least 18 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_get_local_mac(char *mac_str);

/**
 * @brief Add a device to the tracking list
 *
 * @param mac_str MAC address string
 * @param name Device name
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_add_device(const char *mac_str, const char *name);

/**
 * @brief Remove a device from the tracking list
 *
 * @param identifier MAC address or name
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_remove_device(const char *identifier);

/**
 * @brief Save device configuration to NVS
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_save_config(void);

/**
 * @brief Load device configuration from NVS
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_load_config(void);

/**
 * @brief Get JSON string with current device configuration
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_presence_get_config_json(char *buffer, size_t buffer_size);

/**
 * @brief Get number of configured devices
 *
 * @return Number of configured devices
 */
int bt_presence_get_device_count(void);

#endif // BT_PRESENCE_H
