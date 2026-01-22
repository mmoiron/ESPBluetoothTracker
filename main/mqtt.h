#ifndef MQTT_H
#define MQTT_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Callback type for scan requests
 */
typedef void (*mqtt_scan_request_cb_t)(void);

/**
 * @brief Initialize MQTT client and connect to broker
 *
 * @param scan_cb Callback function to call when scan request is received
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_init(mqtt_scan_request_cb_t scan_cb);

/**
 * @brief Publish presence state for a device
 *
 * @param device_name Device name/ID
 * @param present true if device is present, false if absent
 * @param reason Reason string (e.g., "sdp_ok", "sdp_timeout")
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
 * @brief Check if MQTT is connected
 *
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

#endif // MQTT_H
