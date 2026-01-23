#ifndef MQTT_H
#define MQTT_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Callback type for scan requests
 */
typedef void (*mqtt_scan_request_cb_t)(void);

/**
 * @brief Callback type for device configuration changes
 * @param action "add", "remove", "list", or "pairing"
 * @param mac MAC address string (or "on"/"off" for pairing)
 * @param name Device name
 */
typedef void (*mqtt_config_cb_t)(const char *action, const char *mac, const char *name);

/**
 * @brief Initialize MQTT client
 *
 * @param scan_cb Callback for scan requests
 * @param config_cb Callback for configuration changes
 * @param esp32_mac MAC address of this ESP32 (for topic prefix)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_init(mqtt_scan_request_cb_t scan_cb, mqtt_config_cb_t config_cb, const char *esp32_mac);

/**
 * @brief Publish presence state for a device
 *
 * @param device_name Name of the device
 * @param present true if present, false if absent
 * @param reason Reason string for debugging
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_presence(const char *device_name, bool present, const char *reason);

/**
 * @brief Publish scan status
 *
 * @param status Status string (e.g., "started", "completed")
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_scan_status(const char *status);

/**
 * @brief Publish current device configuration (retained)
 *
 * @param devices_json JSON string with device list
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_config(const char *devices_json);

/**
 * @brief Publish pairing mode status
 *
 * @param enabled true if pairing mode is enabled
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_pairing_status(bool enabled);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

#endif // MQTT_H
