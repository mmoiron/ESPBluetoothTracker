#include "provisioning.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "PROV";
static const char *NVS_NAMESPACE = "prov_config";

static httpd_handle_t s_server = NULL;
static bool s_provisioning_done = false;
static EventGroupHandle_t s_prov_event_group = NULL;
#define PROV_DONE_BIT BIT0

// HTML page for configuration
static const char PROV_HTML_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 Presence Setup</title>"
"<style>"
"body{font-family:Arial,sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
"h1{color:#00d4ff;text-align:center;}"
"h2{color:#aaa;font-size:14px;text-align:center;margin-bottom:30px;}"
".container{max-width:400px;margin:0 auto;}"
"form{background:#16213e;padding:25px;border-radius:10px;}"
"label{display:block;margin-bottom:5px;color:#00d4ff;}"
"input{width:100%;padding:12px;margin-bottom:15px;border:1px solid #333;"
"border-radius:5px;background:#0f3460;color:#fff;box-sizing:border-box;}"
"input:focus{border-color:#00d4ff;outline:none;}"
"button{width:100%;padding:15px;background:#00d4ff;color:#1a1a2e;"
"border:none;border-radius:5px;font-size:16px;font-weight:bold;cursor:pointer;}"
"button:hover{background:#00b4d8;}"
".section{margin-bottom:20px;padding-bottom:15px;border-bottom:1px solid #333;}"
".section:last-child{border-bottom:none;margin-bottom:0;padding-bottom:0;}"
".info{font-size:12px;color:#666;margin-top:20px;text-align:center;}"
"</style></head><body>"
"<div class='container'>"
"<h1>ESP32 Presence Detector</h1>"
"<h2>Initial Setup</h2>"
"<form action='/save' method='POST'>"
"<div class='section'>"
"<label>WiFi Network Name (SSID)</label>"
"<input type='text' name='ssid' required maxlength='31'>"
"<label>WiFi Password</label>"
"<input type='password' name='wifi_pass' maxlength='63'>"
"</div>"
"<div class='section'>"
"<label>MQTT Broker URI</label>"
"<input type='text' name='mqtt_uri' placeholder='mqtt://192.168.1.1:1883' required maxlength='127'>"
"<label>MQTT Username</label>"
"<input type='text' name='mqtt_user' maxlength='31'>"
"<label>MQTT Password</label>"
"<input type='password' name='mqtt_pass' maxlength='63'>"
"</div>"
"<button type='submit'>Save &amp; Connect</button>"
"</form>"
"<p class='info'>After saving, the device will restart and connect to your network.</p>"
"</div></body></html>";

static const char PROV_SUCCESS_HTML[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Setup Complete</title>"
"<style>"
"body{font-family:Arial,sans-serif;margin:20px;background:#1a1a2e;color:#eee;text-align:center;}"
"h1{color:#00ff88;margin-top:50px;}"
"p{color:#aaa;}"
"</style></head><body>"
"<h1>Setup Complete!</h1>"
"<p>The device will now restart and connect to your network.</p>"
"<p>You can close this page.</p>"
"</body></html>";

bool prov_is_provisioned(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return false;
    }

    uint8_t configured = 0;
    ret = nvs_get_u8(nvs, "configured", &configured);
    nvs_close(nvs);

    return (ret == ESP_OK && configured == 1);
}

esp_err_t prov_load_config(prov_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(prov_config_t));

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No provisioning config found");
        return ret;
    }

    size_t len;

    len = sizeof(config->wifi_ssid);
    nvs_get_str(nvs, "wifi_ssid", config->wifi_ssid, &len);

    len = sizeof(config->wifi_password);
    nvs_get_str(nvs, "wifi_pass", config->wifi_password, &len);

    len = sizeof(config->mqtt_uri);
    nvs_get_str(nvs, "mqtt_uri", config->mqtt_uri, &len);

    len = sizeof(config->mqtt_username);
    nvs_get_str(nvs, "mqtt_user", config->mqtt_username, &len);

    len = sizeof(config->mqtt_password);
    nvs_get_str(nvs, "mqtt_pass", config->mqtt_password, &len);

    uint8_t configured = 0;
    nvs_get_u8(nvs, "configured", &configured);
    config->configured = (configured == 1);

    nvs_close(nvs);

    ESP_LOGI(TAG, "Loaded config: SSID=%s, MQTT=%s", config->wifi_ssid, config->mqtt_uri);
    return ESP_OK;
}

esp_err_t prov_save_config(const prov_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_str(nvs, "wifi_ssid", config->wifi_ssid);
    nvs_set_str(nvs, "wifi_pass", config->wifi_password);
    nvs_set_str(nvs, "mqtt_uri", config->mqtt_uri);
    nvs_set_str(nvs, "mqtt_user", config->mqtt_username);
    nvs_set_str(nvs, "mqtt_pass", config->mqtt_password);
    nvs_set_u8(nvs, "configured", 1);

    // Clear force_prov flag since provisioning is now complete
    nvs_erase_key(nvs, "force_prov");

    ret = nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Config saved: SSID=%s, MQTT=%s", config->wifi_ssid, config->mqtt_uri);
    return ret;
}

esp_err_t prov_clear_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_all(nvs);

    // Set flag to force provisioning on next boot
    nvs_set_u8(nvs, "force_prov", 1);

    ret = nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Config cleared, force_prov flag set");
    return ret;
}

bool prov_is_forced(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return false;
    }

    uint8_t force_prov = 0;
    ret = nvs_get_u8(nvs, "force_prov", &force_prov);
    nvs_close(nvs);

    return (ret == ESP_OK && force_prov == 1);
}

esp_err_t prov_clear_force_flag(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_key(nvs, "force_prov");
    ret = nvs_commit(nvs);
    nvs_close(nvs);

    return ret;
}

esp_err_t prov_factory_reset(void)
{
    ESP_LOGW(TAG, "*** FACTORY RESET ***");
    ESP_LOGI(TAG, "Clearing all NVS data...");

    // Erase entire NVS partition
    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Reinitialize NVS
    ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reinit NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set force provisioning flag and skip Kconfig defaults flag
    nvs_handle_t nvs;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret == ESP_OK) {
        nvs_set_u8(nvs, "force_prov", 1);
        nvs_set_u8(nvs, "skip_defaults", 1);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Factory reset complete. Restarting...");
    return ESP_OK;
}

bool prov_should_skip_defaults(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return false;
    }

    uint8_t skip = 0;
    ret = nvs_get_u8(nvs, "skip_defaults", &skip);
    nvs_close(nvs);

    return (ret == ESP_OK && skip == 1);
}

// URL decode helper
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char a, b;
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            a = src[1];
            b = src[2];
            a = (a >= 'A') ? ((a & 0xDF) - 'A' + 10) : (a - '0');
            b = (b >= 'A') ? ((b & 0xDF) - 'A' + 10) : (b - '0');
            dst[i++] = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// Parse form field from POST data
static bool parse_form_field(const char *data, const char *field, char *value, size_t value_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);

    const char *start = strstr(data, search);
    if (!start) {
        value[0] = '\0';
        return false;
    }

    start += strlen(search);
    const char *end = strchr(start, '&');

    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= value_size) {
        len = value_size - 1;
    }

    char encoded[256];
    if (len >= sizeof(encoded)) {
        len = sizeof(encoded) - 1;
    }
    memcpy(encoded, start, len);
    encoded[len] = '\0';

    url_decode(value, encoded, value_size);
    return true;
}

// HTTP handler for root page
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PROV_HTML_PAGE, strlen(PROV_HTML_PAGE));
    return ESP_OK;
}

// HTTP handler for form submission
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[512];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf) - 1) {
        remaining = sizeof(buf) - 1;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Received form data: %s", buf);

    prov_config_t config = {0};
    parse_form_field(buf, "ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    parse_form_field(buf, "wifi_pass", config.wifi_password, sizeof(config.wifi_password));
    parse_form_field(buf, "mqtt_uri", config.mqtt_uri, sizeof(config.mqtt_uri));
    parse_form_field(buf, "mqtt_user", config.mqtt_username, sizeof(config.mqtt_username));
    parse_form_field(buf, "mqtt_pass", config.mqtt_password, sizeof(config.mqtt_password));

    if (strlen(config.wifi_ssid) == 0 || strlen(config.mqtt_uri) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID and MQTT URI are required");
        return ESP_FAIL;
    }

    config.configured = true;
    prov_save_config(&config);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PROV_SUCCESS_HTML, strlen(PROV_SUCCESS_HTML));

    s_provisioning_done = true;
    if (s_prov_event_group) {
        xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);
    }

    return ESP_OK;
}

// Captive portal handler - redirect all requests to root
static esp_err_t captive_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    // Root page
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };
    httpd_register_uri_handler(server, &root);

    // Save endpoint
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler
    };
    httpd_register_uri_handler(server, &save);

    // Captive portal - catch all other requests
    httpd_uri_t captive = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_handler
    };
    httpd_register_uri_handler(server, &captive);

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

esp_err_t prov_start(const char *ap_suffix)
{
    ESP_LOGI(TAG, "Starting provisioning mode...");

    s_prov_event_group = xEventGroupCreate();
    s_provisioning_done = false;

    // Initialize WiFi in AP mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Create AP name with suffix
    char ap_ssid[32];
    if (ap_suffix && strlen(ap_suffix) > 0) {
        snprintf(ap_ssid, sizeof(ap_ssid), "ESP32_Presence_%s", ap_suffix);
    } else {
        strncpy(ap_ssid, "ESP32_Presence_Setup", sizeof(ap_ssid));
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           PROVISIONING MODE ACTIVE                 ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  1. Connect to WiFi: %-28s  ║", ap_ssid);
    ESP_LOGI(TAG, "║  2. Open browser: http://192.168.4.1               ║");
    ESP_LOGI(TAG, "║  3. Enter WiFi and MQTT settings                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    // Start HTTP server
    s_server = start_webserver();
    if (s_server == NULL) {
        return ESP_FAIL;
    }

    // Wait for provisioning to complete
    xEventGroupWaitBits(s_prov_event_group, PROV_DONE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Provisioning complete! Restarting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Cleanup and restart
    httpd_stop(s_server);
    esp_wifi_stop();

    esp_restart();

    return ESP_OK; // Never reached
}
