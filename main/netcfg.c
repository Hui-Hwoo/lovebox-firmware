// Lovebox 独立固件 —— 配置持久化 + 设置网页(改 Wi-Fi / 城市 / 单位)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "netcfg.h"
#include "lovebox_config.h"

static const char *T = "netcfg";
lb_cfg_t g_cfg;
char     g_ip[16] = "";

#define NS "lbcfg"

void netcfg_load(void) {
    // 默认值(编译期):首次上电即可用,之后可被网页覆盖
    strncpy(g_cfg.ssid, LB_WIFI_SSID, sizeof(g_cfg.ssid)-1);
    strncpy(g_cfg.pass, LB_WIFI_PASS, sizeof(g_cfg.pass)-1);
    strncpy(g_cfg.city, "San Francisco", sizeof(g_cfg.city)-1);
    g_cfg.unit = 'F';

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(T, "无存储配置,用默认"); return; }
    size_t n;
    n = sizeof(g_cfg.ssid); nvs_get_str(h, "ssid", g_cfg.ssid, &n);
    n = sizeof(g_cfg.pass); nvs_get_str(h, "pass", g_cfg.pass, &n);
    n = sizeof(g_cfg.city); nvs_get_str(h, "city", g_cfg.city, &n);
    uint8_t u; if (nvs_get_u8(h, "unit", &u) == ESP_OK) g_cfg.unit = (char)u;
    nvs_close(h);
    ESP_LOGI(T, "已载入配置: ssid=%s city=%s unit=%c", g_cfg.ssid, g_cfg.city, g_cfg.unit);
}

void netcfg_save(const lb_cfg_t *c) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGE(T, "NVS 打开失败"); return; }
    nvs_set_str(h, "ssid", c->ssid);
    nvs_set_str(h, "pass", c->pass);
    nvs_set_str(h, "city", c->city);
    nvs_set_u8(h, "unit", (uint8_t)c->unit);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(T, "配置已保存(ssid=%s city=%s unit=%c)", c->ssid, c->city, c->unit);  // 不打印密码
}

// ---------- URL 解码(表单 application/x-www-form-urlencoded)----------
static void urldecode(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') *o++ = ' ';
        else if (*p == '%' && p[1] && p[2]) {
            char hx[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(hx, NULL, 16); p += 2;
        } else *o++ = *p;
    }
    *o = 0;
}
// 从表单体取某字段值(解码后写入 out)
static bool form_field(const char *body, const char *key, char *out, int outlen) {
    char pat[40]; snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    // 需确保是字段起始(body 开头或前面是 &)
    while (p && p != body && *(p-1) != '&') p = strstr(p+1, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *e = strchr(p, '&'); int len = e ? (int)(e-p) : (int)strlen(p);
    if (len > outlen-1) len = outlen-1;
    memcpy(out, p, len); out[len] = 0;
    urldecode(out);
    return true;
}

// ---------- 网页 ----------
static esp_err_t root_get(httpd_req_t *req) {
    // 扫描周边 Wi-Fi 供选择(非致命)
    wifi_scan_config_t sc = { .show_hidden = false };
    uint16_t ap_num = 0;
    static wifi_ap_record_t aps[20];
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        ap_num = 20; esp_wifi_scan_get_ap_records(&ap_num, aps);
    }
    static char html[6144];
    int n = snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Lovebox Setup</title><style>"
        "body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px;}"
        "h2{font-weight:600}label{display:block;margin:14px 0 4px;font-size:14px;color:#aaa}"
        "input,select{width:100%%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #444;border-radius:8px;background:#1c1c1c;color:#fff}"
        "button{margin-top:22px;width:100%%;padding:13px;font-size:16px;border:0;border-radius:8px;background:#2d7;color:#000;font-weight:600}"
        ".hint{font-size:12px;color:#777;margin-top:4px}</style></head><body>"
        "<h2>Lovebox 设置</h2><p class=hint>固件版本 %s</p><form method=POST action=/save>"
        "<label>Wi-Fi 名称 (SSID)</label>"
        "<input name=ssid list=aps value=\"%s\" autocomplete=off>"
        "<datalist id=aps>", esp_app_get_description()->version, g_cfg.ssid);
    for (int i = 0; i < ap_num && n < (int)sizeof(html)-300; i++)
        n += snprintf(html+n, sizeof(html)-n, "<option value=\"%s\">", (char*)aps[i].ssid);
    n += snprintf(html+n, sizeof(html)-n,
        "</datalist>"
        "<label>Wi-Fi 密码</label>"
        "<input name=pass type=password placeholder=\"(留空则不修改)\" autocomplete=off>"
        "<label>城市 (英文名,用于天气)</label>"
        "<input name=city value=\"%s\">"
        "<label>温度单位</label>"
        "<select name=unit><option value=F%s>华氏 F</option><option value=C%s>摄氏 C</option></select>"
        "<button type=submit>保存并重启</button>"
        "<p class=hint>保存后设备会重启并用新设置连接。</p>"
        "</form>"
        "<hr style='border-color:#333;margin:28px 0'>"
        "<h2>固件更新</h2>"
        "<button type=button onclick='chk()' style='background:#2d7;color:#000'>立即检查 GitHub 更新</button>"
        "<p class=hint>最近检查:<span id=os>%s</span></p>"
        "<p class=hint>从文件上传(离线):</p>"
        "<input type=file id=fw accept='.bin'>"
        "<button type=button onclick='up()' style='background:#58f;color:#fff'>上传并更新</button>"
        "<p id=st class=hint></p>"
        "<script>"
        "async function chk(){var o=document.getElementById('os');o.textContent='检查中…';"
        "try{await fetch('/checkupdate',{method:'POST'});"
        "o.textContent='已触发,有新版会自动升级并重启。几秒后刷新本页看结果/版本号。';}"
        "catch(e){o.textContent='设备可能正在升级重启…';}}"
        "async function up(){var f=document.getElementById('fw').files[0];var s=document.getElementById('st');"
        "if(!f){s.textContent='请先选择 firmware.bin';return;}"
        "s.textContent='上传中,请勿断电…';"
        "try{var r=await fetch('/ota',{method:'POST',body:f});"
        "s.textContent=r.ok?'更新成功,设备重启中…':('失败: '+await r.text());}"
        "catch(e){s.textContent='上传已结束(设备可能正在重启)';}}"
        "</script>"
        "</body></html>",
        g_cfg.city,
        g_cfg.unit=='F' ? " selected" : "",
        g_cfg.unit=='C' ? " selected" : "",
        g_ota_status);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, n);
    return ESP_OK;
}

static void reboot_task(void *a) { vTaskDelay(pdMS_TO_TICKS(800)); motor_spin(3); esp_restart(); }  // 升级完成转3下

// 网页"立即检查 GitHub 更新"
static esp_err_t checkupdate_post(httpd_req_t *req) {
    trigger_update_check();
    httpd_resp_sendstr(req, "checking");
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req) {
    static char body[1024];
    int total = 0, r;
    while (total < (int)sizeof(body)-1 && (r = httpd_req_recv(req, body+total, sizeof(body)-1-total)) > 0) total += r;
    body[total] = 0;

    lb_cfg_t c = g_cfg;   // 从当前配置起改
    char v[80];
    if (form_field(body, "ssid", v, sizeof(v)) && v[0]) { strncpy(c.ssid, v, sizeof(c.ssid)-1); c.ssid[sizeof(c.ssid)-1]=0; }
    if (form_field(body, "pass", v, sizeof(v)) && v[0]) { strncpy(c.pass, v, sizeof(c.pass)-1); c.pass[sizeof(c.pass)-1]=0; }  // 留空保留旧密码
    if (form_field(body, "city", v, sizeof(v)) && v[0]) { strncpy(c.city, v, sizeof(c.city)-1); c.city[sizeof(c.city)-1]=0; }
    if (form_field(body, "unit", v, sizeof(v)) && (v[0]=='F'||v[0]=='C')) c.unit = v[0];
    netcfg_save(&c);

    const char *ok = "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:system-ui;background:#111;color:#eee;text-align:center;padding:60px 24px}</style>"
        "</head><body><h2>已保存 ✓</h2><p>设备正在重启并用新设置连接…</p></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ---------- 浏览器上传固件(OTA over HTTP upload)----------
static esp_err_t ota_post(httpd_req_t *req) {
    // 用户手动上传视为明确意图:先确认当前固件有效,避免 pending-verify 窗口拒绝新 OTA
    esp_ota_mark_app_valid_cancel_rollback();
    motor_spin(1);                               // 升级开始:转1下
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota part"); return ESP_FAIL; }
    esp_ota_handle_t oh;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &oh) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "begin"); return ESP_FAIL;
    }
    static char buf[2048];
    int r, total = 0;
    while ((r = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
        if (esp_ota_write(oh, buf, r) != ESP_OK) {
            esp_ota_abort(oh); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write"); return ESP_FAIL;
        }
        total += r;
    }
    if (r < 0 || total == 0) { esp_ota_abort(oh); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
    if (esp_ota_end(oh) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "end(校验失败?)"); return ESP_FAIL; }
    if (esp_ota_set_boot_partition(part) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "setboot"); return ESP_FAIL; }
    ESP_LOGI(T, "浏览器上传固件成功(%d 字节)-> %s,重启", total, part->label);
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void netcfg_start_web(void) {
    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;            // OTA 写 flash 需要更大栈
    cfg.recv_wait_timeout = 20;       // 允许较大固件上传
    cfg.send_wait_timeout = 20;
    if (httpd_start(&srv, &cfg) != ESP_OK) { ESP_LOGE(T, "网页服务器启动失败"); return; }
    httpd_uri_t u_root = { .uri="/", .method=HTTP_GET, .handler=root_get };
    httpd_uri_t u_save = { .uri="/save", .method=HTTP_POST, .handler=save_post };
    httpd_uri_t u_ota  = { .uri="/ota", .method=HTTP_POST, .handler=ota_post };
    httpd_uri_t u_chk  = { .uri="/checkupdate", .method=HTTP_POST, .handler=checkupdate_post };
    httpd_register_uri_handler(srv, &u_root);
    httpd_register_uri_handler(srv, &u_save);
    httpd_register_uri_handler(srv, &u_ota);
    httpd_register_uri_handler(srv, &u_chk);
    ESP_LOGI(T, "设置网页已启动(:80),含固件上传 /ota 与检查更新 /checkupdate");
}
