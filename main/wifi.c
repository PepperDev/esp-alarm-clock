#include "wifi.h"
#include "data.h"
#include "dns.h"
#include "setup.h"

#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "lwip/sockets.h"

static esp_netif_t *netif = NULL;
static esp_event_handler_instance_t reconnect_handler = NULL;

static const char *wifi_deinit();
static void wifi_connect(void *, esp_event_base_t, int32_t, void *);

const char *wifi_driver_init()
{
    if (esp_netif_init() != ESP_OK) {
        return "Unable to init network.";
    }

    if (esp_event_loop_create_default() != ESP_OK) {
        return "Unable to create event loop.";
    }
    return NULL;
}

const char *wifi_ap_init(struct data *data)
{
    const char *err;
    if ((err = wifi_deinit())) {
        return err;
    }
    netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return "Unable to init wifi.";
    }
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        return "Unable to set wifi AP mode.";
    }
    wifi_config_t wifi_config = {
        .ap = {
               .ssid = "alarm-clock-setup",
               .ssid_len = 17,
               .password = "",
               .max_connection = 1,
               .authmode = WIFI_AUTH_OPEN,
               .pmf_cfg = {
                           .capable = true,
                           .required = false,
                           },
               .bss_max_idle_cfg = {
                                    .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                                    .protected_keep_alive = true,
                                    },
               },
    };
    if (esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) != ESP_OK) {
        return "Unable to setup wifi AP config.";
    }
    if (esp_wifi_start() != ESP_OK) {
        return "Unable to start wifi AP.";
    }
    // captive portal
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    char url[23] = "http://";
    inet_ntop(AF_INET, &ip_info.ip.addr, url + 7, 16);
    esp_netif_dhcps_stop(netif);
    if (esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, url, strlen(url)) != ESP_OK) {
        return "Unable to set captive portal URL.";
    }
    esp_netif_dhcps_start(netif);
    // dns server
    if ((err = dns_start(&ip_info.ip.addr))) {
        return err;
    }
    SemaphoreHandle_t signal = xSemaphoreCreateBinary();
    if (signal == NULL) {
        return "Unable to create signal mutex.";
    }
    // webserver
    if ((err = setup_server(url, data, &signal))) {
        return err;
    }
    // wait response
    if (*data->ssid) {
        if (xSemaphoreTake(signal, pdMS_TO_TICKS(120000)) != pdTRUE) {
            setup_stop_server();
            dns_stop();
            vSemaphoreDelete(signal);
            return wifi_deinit();
        }
    } else {
        if (xSemaphoreTake(signal, portMAX_DELAY) != pdTRUE) {
            return "Unable to wait for signal.";
        }
    }
    setup_stop_server();
    dns_stop();
    vSemaphoreDelete(signal);
    if ((err = data_write(data))) {
        return err;
    }
    setenv("TZ", data->timezone, 1);
    tzset();
    return wifi_deinit();
}

const char *wifi_sta_init(struct data *data)
{
    const char *err;
    if ((err = wifi_deinit())) {
        return err;
    }
    netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return "Unable to init wifi.";
    }
    SemaphoreHandle_t signal = xSemaphoreCreateBinary();
    if (signal == NULL) {
        wifi_deinit();
        return "Unable to create signal mutex.";
    }
    esp_event_handler_instance_t ip_handler;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_connect, &signal, &ip_handler) !=
        ESP_OK) {
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable the register IP event handler.";
    }
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_connect, NULL, &reconnect_handler) !=
        ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable the register reconnect event handler.";
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable to set wifi station mode.";
    }
    wifi_config_t wifi_config = {
        .sta = {
                .ssid = {0},
                .password = {0},
                .pmf_cfg = {
                            .capable = true,
                            .required = false,
                            },
                },
    };
    memcpy(wifi_config.sta.ssid, data->ssid, strnlen(data->ssid, sizeof(((struct data *) NULL)->ssid) - 1) + 1);
    memcpy(wifi_config.sta.password, data->password,
           strnlen(data->password, sizeof(((struct data *) NULL)->password) - 1) + 1);
    if (esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable to setup wifi station config.";
    }
    if (esp_wifi_start() != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable to start wifi station.";
    }
    esp_err_t esp_err = esp_wifi_connect();
    if (esp_err != ESP_OK && esp_err != ESP_ERR_WIFI_CONN) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable to connect wifi station.";
    }
    if (xSemaphoreTake(signal, pdMS_TO_TICKS(30000)) != pdTRUE) {       // or time / portTICK_PERIOD_MS
        esp_wifi_disconnect();
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        vSemaphoreDelete(signal);
        wifi_deinit();
        return "Unable to wait for IP assigment signal.";
    }
    vSemaphoreDelete(signal);
    if (esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler) != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_wifi_disconnect();
        wifi_deinit();
        return "Unable to release IP event handler.";
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_wifi_disconnect();
        wifi_deinit();
        return "Unable to configure NTP server.";
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000)) != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler);
        esp_netif_sntp_deinit();
        esp_wifi_disconnect();
        wifi_deinit();
        return "Unable to wait for NTP clock update.";
    }
    esp_netif_sntp_deinit();
    return NULL;
}

static const char *wifi_deinit()
{
    if (!netif) {
        return NULL;
    }
    if (reconnect_handler) {
        if (esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, reconnect_handler) != ESP_OK) {
            return "Unable to release reconnect event handler.";
        }
        esp_wifi_disconnect();
        reconnect_handler = NULL;
    }
    if (esp_wifi_deinit() != ESP_OK) {
        return "Unable to deinit wifi.";
    }
    esp_netif_destroy_default_wifi(netif);
    netif = NULL;
    return NULL;
}

static void wifi_connect(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xSemaphoreGive(*(SemaphoreHandle_t *) arg);
    } else if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_connect();
        }
    }
}
