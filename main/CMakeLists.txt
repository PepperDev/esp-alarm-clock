idf_component_register(SRCS "main.c" "data.c" "log.c" "wifi.c" "dns.c" "setup.c" "form.c" "alarm.c"
					PRIV_REQUIRES esp_event nvs_flash esp_wifi esp_netif esp_http_server json esp_driver_ledc
                    INCLUDE_DIRS "."
                    EMBED_FILES setup.html alarm.html)
