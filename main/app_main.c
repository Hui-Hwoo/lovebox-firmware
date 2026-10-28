// Lovebox 自定义固件 v2 —— 独立内核(不再依赖 Mac)
// 连网(NVS 可配)-> 连上则跑仪表盘+设置网页+尽力OTA;连不上则开热点让用户配网。
// 关键:不再因「连不上服务器」回滚到原厂。只要能开机运行,就保住本固件。
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "lovebox_config.h"
#include "netcfg.h"

// GitHub Releases 自动更新源(公开仓库,免登录拉取)
#define GH_OTA_URL "https://github.com/Hui-Hwoo/lovebox-firmware/releases/latest/download/firmware.bin"
#define GH_POLL_S  300            // 每 5 分钟查一次(只在远端版本更高时才升级)

// 常驻配网热点(APSTA:连家里网的同时一直广播,随时可访问设置页)
#define AP_SSID "Lovebox-Setup"
#define AP_PASS "lovebox888"      // WPA2 密码,防止陌生人改设置/刷固件

static const char *TAG = "lovebox";
static EventGroupHandle_t s_eg;
#define WIFI_OK    BIT0
#define CHECK_NOW  BIT1          // 网页"立即检查更新"触发
static int s_retry = 0;

char g_ota_status[80] = "尚未检查";   // 最近一次 GitHub 检查结果(供网页显示)

// 电机转动反馈(GPIO32 高电平转):升级开始转1下,完成转3下
void motor_spin(int times) {
    gpio_reset_pin(32); gpio_set_direction(32, GPIO_MODE_OUTPUT);
    for (int i = 0; i < times; i++) {
        gpio_set_level(32, 1); vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(32, 0); vTaskDelay(pdMS_TO_TICKS(260));
    }
}

// 供网页端触发立即检查
void trigger_update_check(void) { if (s_eg) xEventGroupSetBits(s_eg, CHECK_NOW); }

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // 永不放弃:每次断开都重连家里 Wi-Fi(驱动自带节流,不会狂刷)
        xEventGroupClearBits(s_eg, WIFI_OK);
        s_retry++;
        if (s_retry % 10 == 1) ESP_LOGW(TAG, "Wi-Fi 未连上,持续重试中(第%d次)", s_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t*)data;
        snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "已连上 Wi-Fi,IP=%s", g_ip);
        s_retry = 0;
        xEventGroupSetBits(s_eg, WIFI_OK);
    }
}

static esp_netif_t *s_sta_netif, *s_ap_netif;

// APSTA 常驻:连家里 Wi-Fi(STA)的同时,一直广播 Lovebox-Setup 热点(AP)
static void wifi_start(void) {
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    wifi_config_t sta = { 0 };
    strncpy((char*)sta.sta.ssid, g_cfg.ssid, sizeof(sta.sta.ssid)-1);
    strncpy((char*)sta.sta.password, g_cfg.pass, sizeof(sta.sta.password)-1);
    wifi_config_t ap = { 0 };
    strncpy((char*)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid)-1);
    ap.ap.ssid_len = strlen(AP_SSID);
    strncpy((char*)ap.ap.password, AP_PASS, sizeof(ap.ap.password)-1);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;      // 带密码,防陌生人
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "APSTA 启动:连接 %s,并常驻热点 %s(192.168.4.1)", g_cfg.ssid, AP_SSID);
}

// 若开机 90s 没连上家里网(进了配网屏),后台一旦连上就重启回正常仪表盘
static void wifi_watch_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (xEventGroupGetBits(s_eg) & WIFI_OK) { ESP_LOGI(TAG, "家里 Wi-Fi 已恢复,重启回正常模式"); esp_restart(); }
    }
}

// ---------- OTA 自更新:尽力而为,Mac 不在就跳过,绝不致命 ----------
static void ota_task(void *arg) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/fw/firmware.bin", LB_SERVER_IP, LB_SERVER_PORT);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LB_OTA_POLL_S * 1000));
        if (!(xEventGroupGetBits(s_eg) & WIFI_OK)) continue;
        esp_http_client_config_t hc = { .url = url, .timeout_ms = 15000, .keep_alive_enable = true };
        esp_https_ota_config_t oc = { .http_config = &hc };
        esp_https_ota_handle_t h = NULL;
        if (esp_https_ota_begin(&oc, &h) != ESP_OK || !h) continue;   // Mac 不在 -> 静默跳过
        esp_app_desc_t nd;
        if (esp_https_ota_get_img_desc(h, &nd) != ESP_OK) { esp_https_ota_abort(h); continue; }
        const esp_app_desc_t *cd = esp_app_get_description();
        if (strcmp(nd.version, cd->version) == 0) { esp_https_ota_abort(h); continue; }
        ESP_LOGI(TAG, "OTA 新版本 %s -> %s", cd->version, nd.version);
        motor_spin(1);                               // 升级开始:转1下
        esp_err_t r;
        while ((r = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) { }
        if (r == ESP_OK && esp_https_ota_is_complete_data_received(h) && esp_https_ota_finish(h) == ESP_OK) {
            ESP_LOGI(TAG, "OTA 成功,重启"); motor_spin(3); esp_restart();   // 完成:转3下
        } else { ESP_LOGE(TAG, "OTA 失败 r=%d", r); esp_https_ota_abort(h); }
    }
}

// 比较版本号 "a.b.c":remote 是否严格新于 local(GitHub 只升不降)
static bool ver_newer(const char *r, const char *l) {
    int ra[3] = {0,0,0}, la[3] = {0,0,0};
    sscanf(r, "%d.%d.%d", &ra[0], &ra[1], &ra[2]);
    sscanf(l, "%d.%d.%d", &la[0], &la[1], &la[2]);
    for (int i = 0; i < 3; i++) if (ra[i] != la[i]) return ra[i] > la[i];
    return false;
}

// 用 GitHub API 查最新版本号(直连 HTTPS,无重定向,小响应,易诊断)
#define GH_API_URL "https://api.github.com/repos/Hui-Hwoo/lovebox-firmware/releases/latest"
static bool gh_latest_version(char *out, int outlen) {
    static char buf[2560];
    esp_http_client_config_t c = { .url = GH_API_URL, .crt_bundle_attach = esp_crt_bundle_attach,
                                   .timeout_ms = 20000, .user_agent = "lovebox-esp32" };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    esp_err_t err = esp_http_client_open(h, 0);
    if (err != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "API连接失败 0x%x", err); esp_http_client_cleanup(h); return false; }
    esp_http_client_fetch_headers(h);
    int sc = esp_http_client_get_status_code(h);
    int n = esp_http_client_read_response(h, buf, sizeof(buf)-1);
    buf[n > 0 ? n : 0] = 0;
    esp_http_client_close(h); esp_http_client_cleanup(h);
    if (sc != 200) { snprintf(g_ota_status, sizeof(g_ota_status), "API状态码 %d", sc); return false; }
    char *p = strstr(buf, "\"tag_name\"");
    if (!p) { snprintf(g_ota_status, sizeof(g_ota_status), "解析tag失败"); return false; }
    p = strchr(p, ':'); if (p) p = strchr(p, '"'); if (!p) return false; p++;
    char *q = strchr(p, '"'); if (!q) return false; *q = 0;
    if (*p == 'v') p++;
    strncpy(out, p, outlen-1); out[outlen-1] = 0;
    return true;
}

// 手动跟随重定向下载固件并写入 OTA 分区(github.com -> githubusercontent CDN)
static void gh_download_and_flash(void) {
    esp_http_client_config_t c = { .url = GH_OTA_URL, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000, .user_agent = "lovebox-esp32", .buffer_size = 4096, .buffer_size_tx = 2048,
        .disable_auto_redirect = true };
    esp_http_client_handle_t cl = esp_http_client_init(&c);
    esp_err_t err = esp_http_client_open(cl, 0);
    if (err != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "下载连接失败 0x%x", err); esp_http_client_cleanup(cl); return; }
    esp_http_client_fetch_headers(cl);
    int sc = esp_http_client_get_status_code(cl), redir = 0;
    while ((sc==301||sc==302||sc==303||sc==307||sc==308) && redir++ < 6) {
        esp_http_client_set_redirection(cl);                 // 从 Location 更新 URL
        err = esp_http_client_open(cl, 0);
        if (err != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "跳转%d连接失败 0x%x", redir, err); esp_http_client_cleanup(cl); return; }
        esp_http_client_fetch_headers(cl);
        sc = esp_http_client_get_status_code(cl);
    }
    if (sc != 200) { snprintf(g_ota_status, sizeof(g_ota_status), "下载状态码 %d (跳转%d次)", sc, redir); esp_http_client_cleanup(cl); return; }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t oh;
    if (!part || esp_ota_begin(part, OTA_SIZE_UNKNOWN, &oh) != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "OTA begin失败"); esp_http_client_cleanup(cl); return; }
    static char buf[4096]; int total = 0, r;
    while ((r = esp_http_client_read(cl, buf, sizeof(buf))) > 0) {
        if (esp_ota_write(oh, buf, r) != ESP_OK) { esp_ota_abort(oh); esp_http_client_cleanup(cl); snprintf(g_ota_status, sizeof(g_ota_status), "写入失败 @%d", total); return; }
        total += r;
    }
    esp_http_client_close(cl); esp_http_client_cleanup(cl);
    if (total < 100000) { esp_ota_abort(oh); snprintf(g_ota_status, sizeof(g_ota_status), "下载不完整 n=%d", total); return; }
    if (esp_ota_end(oh) != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "校验失败 n=%d", total); return; }
    if (esp_ota_set_boot_partition(part) != ESP_OK) { snprintf(g_ota_status, sizeof(g_ota_status), "设置启动分区失败"); return; }
    ESP_LOGI(TAG, "GitHub OTA 成功 %d 字节,重启", total); motor_spin(3); esp_restart();
}

// 执行一次 GitHub 检查:有新版则下载升级(开始转1下、完成转3下并重启),结果写入 g_ota_status
static void do_github_check(void) {
    if (!(xEventGroupGetBits(s_eg) & WIFI_OK)) { snprintf(g_ota_status, sizeof(g_ota_status), "无网络,跳过"); return; }
    char rv[32];
    if (!gh_latest_version(rv, sizeof(rv))) return;       // g_ota_status 已含错误原因
    const char *cv = esp_app_get_description()->version;
    if (!ver_newer(rv, cv)) { snprintf(g_ota_status, sizeof(g_ota_status), "已是最新 (%s)", cv); ESP_LOGI(TAG, "GitHub: %s", g_ota_status); return; }

    ESP_LOGI(TAG, "GitHub OTA %s -> %s", cv, rv);
    snprintf(g_ota_status, sizeof(g_ota_status), "升级中 %s -> %s", cv, rv);
    motor_spin(1);                                        // 升级开始:转1下
    gh_download_and_flash();                              // 成功会重启;失败把原因写入 g_ota_status
}

// GitHub 自动更新:开机120s后首查,之后每5分钟,或被网页"立即检查"触发
static void github_ota_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(120000));   // 越过90s健康确认窗口
    while (1) {
        do_github_check();
        // 等 5 分钟,或被手动触发(清除标志)提前唤醒
        xEventGroupWaitBits(s_eg, CHECK_NOW, pdTRUE, pdFALSE, pdMS_TO_TICKS(GH_POLL_S * 1000));
    }
}

// 健康确认:稳定运行一段时间才标记固件有效。坏固件若在此前崩溃/重启,
// 引导器会自动回滚到上一个好版本(双分区 + CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)。
static void health_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(90000));   // 稳定运行 90 秒 = 认定健康
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "运行90s稳定,已标记固件有效");
    vTaskDelete(NULL);
}

static void motor_off(void) {
    const int pins[] = {32};   // 电机 GPIO32,开机拉低(其余显示脚由 display_init 管)
    for (unsigned i=0;i<sizeof(pins)/sizeof(int);i++){ gpio_reset_pin(pins[i]); gpio_set_direction(pins[i],GPIO_MODE_OUTPUT); gpio_set_level(pins[i],0); }
}

void app_main(void) {
    motor_off();
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }

    netcfg_load();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_eg = xEventGroupCreate();

    const esp_partition_t *run = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "启动 v2。运行分区=%s", run ? run->label : "?");

    // 健康确认计时器:稳定跑满90s才标记有效;期间若崩溃则引导器自动回滚到上个好版本
    xTaskCreate(health_task, "health", 3072, NULL, 7, NULL);

    wifi_start();   // APSTA:连家里网 + 常驻 Lovebox-Setup 热点
    // 给家里 Wi-Fi 充足时间(90s),期间持续重试,绝不因一时慢就放弃
    EventBits_t bits = xEventGroupWaitBits(s_eg, WIFI_OK, pdFALSE, pdFALSE, pdMS_TO_TICKS(90000));

    extern void dashboard_run(void);
    extern void dashboard_setup_screen(void);

    // 设置网页始终开启(STA 的 IP 和 热点 192.168.4.1 都能访问)
    netcfg_start_web();

    if (bits & WIFI_OK) {
        xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);          // 局域网 OTA(Mac 在才生效)
        xTaskCreate(github_ota_task, "ghota", 9216, NULL, 5, NULL); // GitHub HTTPS 自动更新(5分钟)
        xTaskCreate((TaskFunction_t)dashboard_run, "dash", 10240, NULL, 4, NULL);
        ESP_LOGI(TAG, "就绪:仪表盘运行,设置页 http://%s 或 http://192.168.4.1", g_ip);
    } else {
        // 90s 还没连上家里网 -> 显示配网屏;热点已常驻,STA 后台继续试,连上自动重启回正常
        xTaskCreate((TaskFunction_t)dashboard_setup_screen, "setup", 8192, NULL, 4, NULL);
        xTaskCreate(wifi_watch_task, "wifiwatch", 3072, NULL, 6, NULL);
        ESP_LOGW(TAG, "未连上家里网,显示配网屏:连 %s -> 打开 192.168.4.1(后台仍在试)", AP_SSID);
    }
}
