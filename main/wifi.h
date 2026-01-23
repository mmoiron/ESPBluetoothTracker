#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize and connect to WiFi
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Get current WiFi SSID
 *
 * @param ssid Buffer to store SSID
 * @param len Buffer size
 */
void wifi_get_ssid(char *ssid, size_t len);

#endif // WIFI_H
