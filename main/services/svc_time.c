// 时间服务:SNTP 对时,时区固定太平洋(自动夏令时)。
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "svc_time.h"

static const char *T = "svc_time";
static volatile bool s_ready = false;
static bool s_started = false;

static void on_sync(struct timeval *tv) { s_ready = true; ESP_LOGI(T, "SNTP 已对时"); }

void svc_time_start(void) {
    if (s_started) return;
    s_started = true;
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);   // 太平洋时区 + 自动夏令时
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    esp_netif_sntp_init(&cfg);
}

bool svc_time_ready(void) { return s_ready; }
