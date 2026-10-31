idf_component_register(
    SRCS
        "app_main.c"
        "display.c"
        "netcfg.c"
        "services/svc_time.c"
        "services/svc_weather.c"
        "services/cfg_store.c"
        "ui/ui_draw.c"
        "ui/splash.c"
        "screens/screen_manager.c"
        "screens/scr_clockwx.c"
        "screens/scr_bigclock.c"
    INCLUDE_DIRS "." "services" "ui" "screens"
    EMBED_FILES "www/index.html" "www/style.css" "www/app.js"
    REQUIRES esp_http_client esp_https_ota app_update nvs_flash esp_wifi esp_netif driver esp-tls mbedtls json lwip esp_http_server esp_timer)
