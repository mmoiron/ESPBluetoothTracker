#ifndef PROVISIONING_H
#define PROVISIONING_H

#include "esp_err.h"
#include <stdbool.h>

#define PROV_SSID_MAX_LEN 32
#define PROV_PASS_MAX_LEN 64
#define PROV_MQTT_URI_MAX_LEN 128
#define PROV_MQTT_USER_MAX_LEN 32
#define PROV_MQTT_PASS_MAX_LEN 64

/**
 * @brief Provisioning configuration structure
 */
typedef struct {
    char wifi_ssid[PROV_SSID_MAX_LEN];
    char wifi_password[PROV_PASS_MAX_LEN];
    char mqtt_uri[PROV_MQTT_URI_MAX_LEN];
    char mqtt_username[PROV_MQTT_USER_MAX_LEN];
    char mqtt_password[PROV_MQTT_PASS_MAX_LEN];
    bool configured;
} prov_config_t;

/**
 * @brief Check if device is provisioned (has saved config)
 *
 * @return true if provisioned, false otherwise
 */
bool prov_is_provisioned(void);

/**
 * @brief Load provisioning config from NVS
 *
 * @param config Pointer to config structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_load_config(prov_config_t *config);

/**
 * @brief Save provisioning config to NVS
 *
 * @param config Pointer to config structure to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_save_config(const prov_config_t *config);

/**
 * @brief Clear provisioning config from NVS
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_clear_config(void);

/**
 * @brief Check if provisioning is forced (via reprovision command)
 *
 * @return true if forced provisioning is pending
 */
bool prov_is_forced(void);

/**
 * @brief Clear the force provisioning flag
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_clear_force_flag(void);

/**
 * @brief Perform factory reset (clear all config including devices)
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_factory_reset(void);

/**
 * @brief Check if Kconfig defaults should be skipped (after factory reset)
 *
 * @return true if defaults should be skipped
 */
bool prov_should_skip_defaults(void);

/**
 * @brief Start provisioning mode (AP + HTTP server)
 *
 * This function blocks until provisioning is complete.
 * The device will restart after successful provisioning.
 *
 * @param ap_suffix Suffix for AP name (e.g., "4A56" from MAC)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t prov_start(const char *ap_suffix);

#endif // PROVISIONING_H
