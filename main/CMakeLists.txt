idf_component_register(SRCS "app_main.c" "display.c" "dashboard.c" "netcfg.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_client esp_https_ota app_update nvs_flash esp_wifi esp_netif driver esp-tls mbedtls json lwip esp_http_server)
