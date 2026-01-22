#ifndef BT_PRESENCE_H
#define BT_PRESENCE_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Bluetooth device info structure
 */
typedef struct {
    uint8_t mac[6];
    char name[32];
    bool present;
    char reason[32];
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

#endif // BT_PRESENCE_H
