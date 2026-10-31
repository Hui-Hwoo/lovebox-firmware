// AlgoStone 控制台后端:静态资源(嵌入的 HTML/CSS/JS)+ JSON 接口。
// 系统级配置(Wi-Fi / 当前画面)存 lbcfg 命名空间;画面各自的设置见 cfg_store。
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "netcfg.h"
#include "lovebox_config.h"
#include "cfg_store.h"
#include "screen_manager.h"
#include "screen.h"

static const char *T = "netcfg";
lb_cfg_t g_cfg;
char     g_ip[16] = "";

#define NS "lbcfg"

// ---- 嵌入的前端资源(见 main/www/,CMakeLists 的 EMBED_FILES)----
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// ============ 配置持久化(系统级)============
void netcfg_load(void) {
    strncpy(g_cfg.ssid, LB_WIFI_SSID, sizeof(g_cfg.ssid)-1);
    strncpy(g_cfg.pass, LB_WIFI_PASS, sizeof(g_cfg.pass)-1);
    strncpy(g_cfg.screen, "clockwx", sizeof(g_cfg.screen)-1);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(T, "无存储配置,用默认"); return; }
    size_t n;
    n = sizeof(g_cfg.ssid);   nvs_get_str(h, "ssid", g_cfg.ssid, &n);
    n = sizeof(g_cfg.pass);   nvs_get_str(h, "pass", g_cfg.pass, &n);
    n = sizeof(g_cfg.screen); nvs_get_str(h, "screen", g_cfg.screen, &n);
    nvs_close(h);
    ESP_LOGI(T, "已载入配置: ssid=%s screen=%s", g_cfg.ssid, g_cfg.screen);
}

void netcfg_save(const lb_cfg_t *c) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGE(T, "NVS 打开失败"); return; }
    nvs_set_str(h, "ssid", c->ssid);
    nvs_set_str(h, "pass", c->pass);
    nvs_set_str(h, "screen", c->screen);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(T, "配置已保存(ssid=%s screen=%s)", c->ssid, c->screen);  // 不打印密码
}

static void save_screen_only(const char *id) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "screen", id);
    nvs_commit(h);
    nvs_close(h);
}

// ============ 通用助手 ============
static void reboot_task(void *a) { vTaskDelay(pdMS_TO_TICKS(800)); motor_spin(3); esp_restart(); }

// 读取请求体到缓冲(返回长度,-1 出错)
static int read_body(httpd_req_t *req, char *buf, int max) {
    int total = 0, r = 0;
    while (total < max-1 && (r = httpd_req_recv(req, buf+total, max-1-total)) > 0) total += r;
    if (r < 0) return -1;
    buf[total] = 0;
    return total;
}

static esp_err_t send_asset(httpd_req_t *req, const uint8_t *s, const uint8_t *e, const char *ctype) {
    httpd_resp_set_type(req, ctype);
    httpd_resp_send(req, (const char*)s, e - s);
    return ESP_OK;
}

// ============ 静态资源 ============
static esp_err_t h_index(httpd_req_t *req) { return send_asset(req, index_html_start, index_html_end, "text/html; charset=utf-8"); }
static esp_err_t h_css(httpd_req_t *req)   { return send_asset(req, style_css_start, style_css_end, "text/css; charset=utf-8"); }
static esp_err_t h_js(httpd_req_t *req)    { return send_asset(req, app_js_start, app_js_end, "application/javascript; charset=utf-8"); }

// ============ JSON 接口 ============
static const char *fld_type_str(fld_type_t t) {
    switch (t) { case FLD_SECRET: return "secret"; case FLD_SELECT: return "select"; default: return "text"; }
}

// GET /api/state —— 设备状态 + 全部画面及其字段(当前值;密钥只给 set 布尔)
static esp_err_t api_state(httpd_req_t *req) {
    const esp_app_desc_t *d = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", d->version);
    char build[48]; snprintf(build, sizeof(build), "%s %s", d->date, d->time);
    cJSON_AddStringToObject(root, "build", build);
    cJSON_AddStringToObject(root, "partition", esp_ota_get_running_partition()->label);
    cJSON_AddStringToObject(root, "ip", g_ip);
    cJSON_AddStringToObject(root, "wifi_ssid", g_cfg.ssid);
    cJSON_AddStringToObject(root, "current", screen_manager_current_id());
    cJSON_AddStringToObject(root, "ota_status", g_ota_status);
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time()/1000000));

    cJSON *arr = cJSON_AddArrayToObject(root, "screens");
    for (int i = 0; i < screen_manager_count(); i++) {
        const screen_t *s = screen_manager_get(i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", s->id);
        cJSON_AddStringToObject(o, "name", s->name);
        cJSON *fa = cJSON_AddArrayToObject(o, "fields");
        for (int k = 0; k < s->n_fields; k++) {
            const scr_field_t *f = &s->fields[k];
            cJSON *fo = cJSON_CreateObject();
            cJSON_AddStringToObject(fo, "key", f->key);
            cJSON_AddStringToObject(fo, "label", f->label);
            cJSON_AddStringToObject(fo, "type", fld_type_str(f->type));
            if (f->options) cJSON_AddStringToObject(fo, "options", f->options);
            if (f->type == FLD_SECRET) {
                cJSON_AddBoolToObject(fo, "set", cfg_has(s->id, f->key));
            } else {
                char val[96]; cfg_get(s->id, f->key, val, sizeof(val), f->def ? f->def : "");
                cJSON_AddStringToObject(fo, "value", val);
            }
            cJSON_AddItemToArray(fa, fo);
        }
        cJSON_AddItemToArray(arr, o);
    }

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, out ? out : "{}");
    cJSON_free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/screen  {"id":"bigclock"} —— 热切换画面(不重启)
static esp_err_t api_screen(httpd_req_t *req) {
    char body[128]; if (read_body(req, body, sizeof(body)) < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    cJSON *id = j ? cJSON_GetObjectItem(j, "id") : NULL;
    if (!cJSON_IsString(id) || !id->valuestring[0]) { cJSON_Delete(j); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no id"); return ESP_FAIL; }
    strncpy(g_cfg.screen, id->valuestring, sizeof(g_cfg.screen)-1); g_cfg.screen[sizeof(g_cfg.screen)-1] = 0;
    save_screen_only(g_cfg.screen);
    screen_manager_select(g_cfg.screen);
    ESP_LOGI(T, "切换画面 -> %s", g_cfg.screen);
    cJSON_Delete(j);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/config  {"screen":"clockwx","fields":{"city":"Tokyo","unit":"C"}}
// 保存某画面的字段(空值视为不修改,天然保护密钥);若是当前画面则热重载。
static esp_err_t api_config(httpd_req_t *req) {
    char body[768]; if (read_body(req, body, sizeof(body)) < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    cJSON *scr = j ? cJSON_GetObjectItem(j, "screen") : NULL;
    cJSON *fields = j ? cJSON_GetObjectItem(j, "fields") : NULL;
    if (!cJSON_IsString(scr) || !cJSON_IsObject(fields)) { cJSON_Delete(j); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad"); return ESP_FAIL; }
    const char *sid = scr->valuestring;
    int saved = 0;
    for (cJSON *f = fields->child; f; f = f->next) {
        if (!cJSON_IsString(f) || !f->valuestring[0]) continue;   // 空 = 不改(含密钥不覆盖)
        cfg_set(sid, f->string, f->valuestring);
        saved++;
    }
    ESP_LOGI(T, "画面 %s 配置已保存(%d 项)", sid, saved);
    cJSON_Delete(j);
    // 若改的是当前画面,立即重载(重取数据 + 重绘)
    if (strcmp(sid, screen_manager_current_id()) == 0) screen_manager_reload();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/wifi  {"ssid":"...","pass":"..."} —— 保存 Wi-Fi 并重启
static esp_err_t api_wifi(httpd_req_t *req) {
    char body[256]; if (read_body(req, body, sizeof(body)) < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    cJSON *ssid = j ? cJSON_GetObjectItem(j, "ssid") : NULL;
    cJSON *pass = j ? cJSON_GetObjectItem(j, "pass") : NULL;
    if (cJSON_IsString(ssid) && ssid->valuestring[0]) { strncpy(g_cfg.ssid, ssid->valuestring, sizeof(g_cfg.ssid)-1); g_cfg.ssid[sizeof(g_cfg.ssid)-1]=0; }
    if (cJSON_IsString(pass) && pass->valuestring[0]) { strncpy(g_cfg.pass, pass->valuestring, sizeof(g_cfg.pass)-1); g_cfg.pass[sizeof(g_cfg.pass)-1]=0; }  // 留空保留旧密码
    netcfg_save(&g_cfg);
    cJSON_Delete(j);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// POST /checkupdate —— 立即检查 GitHub 更新
static esp_err_t api_checkupdate(httpd_req_t *req) {
    trigger_update_check();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /ota —— 浏览器上传固件(原样保留)
static esp_err_t api_ota(httpd_req_t *req) {
    esp_ota_mark_app_valid_cancel_rollback();   // 用户手动上传视为明确意图
    motor_spin(1);
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota part"); return ESP_FAIL; }
    esp_ota_handle_t oh;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &oh) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "begin"); return ESP_FAIL; }
    static char buf[2048];
    int r, total = 0;
    while ((r = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
        if (esp_ota_write(oh, buf, r) != ESP_OK) { esp_ota_abort(oh); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write"); return ESP_FAIL; }
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
    cfg.recv_wait_timeout = 20;
    cfg.send_wait_timeout = 20;
    cfg.max_uri_handlers = 12;        // 静态 3 + 接口若干
    if (httpd_start(&srv, &cfg) != ESP_OK) { ESP_LOGE(T, "网页服务器启动失败"); return; }

    httpd_uri_t routes[] = {
        { .uri="/",            .method=HTTP_GET,  .handler=h_index },
        { .uri="/style.css",   .method=HTTP_GET,  .handler=h_css },
        { .uri="/app.js",      .method=HTTP_GET,  .handler=h_js },
        { .uri="/api/state",   .method=HTTP_GET,  .handler=api_state },
        { .uri="/api/screen",  .method=HTTP_POST, .handler=api_screen },
        { .uri="/api/config",  .method=HTTP_POST, .handler=api_config },
        { .uri="/api/wifi",    .method=HTTP_POST, .handler=api_wifi },
        { .uri="/checkupdate", .method=HTTP_POST, .handler=api_checkupdate },
        { .uri="/ota",         .method=HTTP_POST, .handler=api_ota },
    };
    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++)
        httpd_register_uri_handler(srv, &routes[i]);
    ESP_LOGI(T, "控制台已启动(:80):静态资源 + JSON 接口");
}
