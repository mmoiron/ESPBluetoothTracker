#include "wifi.h"
#include "provisioning.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include <string.h>

static const char *TAG = "WIFI";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to AP (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to AP after %d attempts", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Check if static IP should be configured
    prov_config_t prov_config;
    bool use_static_ip = false;
    if (prov_is_provisioned() && prov_load_config(&prov_config) == ESP_OK && prov_config.use_static_ip) {
        // Validate IP address format (must contain at least 3 dots)
        int dot_count = 0;
        for (int i = 0; prov_config.static_ip[i]; i++) {
            if (prov_config.static_ip[i] == '.') dot_count++;
        }

        if (dot_count >= 3 && strlen(prov_config.static_ip) >= 7) {
            use_static_ip = true;
            ESP_LOGI(TAG, "Configuring static IP: %s", prov_config.static_ip);

            // Stop DHCP client
            esp_netif_dhcpc_stop(sta_netif);

            // Configure static IP
            esp_netif_ip_info_t ip_info = {0};
            ip4addr_aton(prov_config.static_ip, (ip4_addr_t *)&ip_info.ip);
            ip4addr_aton(prov_config.gateway, (ip4_addr_t *)&ip_info.gw);
            ip4addr_aton(prov_config.subnet, (ip4_addr_t *)&ip_info.netmask);

            esp_netif_set_ip_info(sta_netif, &ip_info);

            // Configure DNS
            esp_netif_dns_info_t dns_info = {0};
            ip4addr_aton(prov_config.dns, (ip4_addr_t *)&dns_info.ip.u_addr.ip4);
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);

            ESP_LOGI(TAG, "Static IP configured: IP=%s, GW=%s, DNS=%s",
                     prov_config.static_ip, prov_config.gateway, prov_config.dns);
        } else {
            ESP_LOGW(TAG, "Invalid static IP config ('%s'), using DHCP", prov_config.static_ip);
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Load WiFi credentials - reuse prov_config if already loaded, otherwise load or use Kconfig
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (use_static_ip || (prov_is_provisioned() && prov_load_config(&prov_config) == ESP_OK)) {
        // prov_config is already loaded (either for static IP or just now)
        ESP_LOGI(TAG, "Using provisioned WiFi config");
        strncpy((char *)wifi_config.sta.ssid, prov_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, prov_config.wifi_password, sizeof(wifi_config.sta.password));
    } else {
        ESP_LOGI(TAG, "Using Kconfig default WiFi config");
        strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init complete, connecting to %s...", wifi_config.sta.ssid);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_config.sta.ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", wifi_config.sta.ssid);
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

bool wifi_is_connected(void)
{
    return s_is_connected;
}

void wifi_get_ssid(char *ssid, size_t len)
{
    prov_config_t prov_config;
    if (prov_is_provisioned() && prov_load_config(&prov_config) == ESP_OK) {
        strncpy(ssid, prov_config.wifi_ssid, len);
    } else {
        strncpy(ssid, CONFIG_WIFI_SSID, len);
    }
}
