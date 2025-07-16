idf_component_register(SRCS "particle-monitor.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_netif esp_event nvs_flash esp_http_server json)
