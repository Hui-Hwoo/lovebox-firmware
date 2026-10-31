// 天气服务:地理编码(open-meteo geocoding)+ 当前天气(open-meteo forecast),均走 HTTP。
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "svc_weather.h"
#include "netcfg.h"      // g_cfg.unit

static const char *T = "svc_weather";

static double s_lat = 37.7749, s_lon = -122.4194;   // 回落:旧金山
static weather_t s_wx = { .ok = false, .temp = 0, .unit = 'F', .wcode = -1 };

// ---------- 简易 HTTP GET:把响应体读进调用者缓冲 ----------
static int http_get(const char *url, char *buf, int buflen) {
    esp_http_client_config_t c = { .url = url, .timeout_ms = 10000 };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    int total = -1;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        int n, off = 0;
        while ((n = esp_http_client_read(h, buf + off, buflen - 1 - off)) > 0) {
            off += n; if (off >= buflen - 1) break;
        }
        buf[off] = 0;
        total = off;
        esp_http_client_close(h);
    }
    esp_http_client_cleanup(h);
    return total;
}

void svc_weather_geocode(const char *city) {
    static char url[256], buf[1024];
    char q[80]; int qi = 0;                       // 把空格换成 + 做查询
    for (const char *p = city; *p && qi < (int)sizeof(q)-1; p++) q[qi++] = (*p==' ') ? '+' : *p;
    q[qi] = 0;
    snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&format=json", q);
    int n = http_get(url, buf, sizeof(buf));
    if (n <= 0) { ESP_LOGW(T, "地理编码失败,用上次/回落坐标"); return; }
    cJSON *j = cJSON_Parse(buf);
    if (!j) { ESP_LOGW(T, "地理编码 JSON 解析失败"); return; }
    cJSON *res = cJSON_GetObjectItem(j, "results");
    cJSON *r0 = res ? cJSON_GetArrayItem(res, 0) : NULL;
    if (r0) {
        cJSON *lat = cJSON_GetObjectItem(r0, "latitude");
        cJSON *lon = cJSON_GetObjectItem(r0, "longitude");
        if (cJSON_IsNumber(lat)) s_lat = lat->valuedouble;
        if (cJSON_IsNumber(lon)) s_lon = lon->valuedouble;
        ESP_LOGI(T, "城市 %s -> (%.3f, %.3f)", city, s_lat, s_lon);
    } else ESP_LOGW(T, "城市 %s 未找到,用回落坐标", city);
    cJSON_Delete(j);
}

void svc_weather_refresh(void) {
    static char url[256], buf[1536];
    const char *unit = (g_cfg.unit == 'C') ? "celsius" : "fahrenheit";
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&temperature_unit=%s",
        s_lat, s_lon, unit);
    int n = http_get(url, buf, sizeof(buf));
    if (n <= 0) { ESP_LOGW(T, "天气请求失败"); return; }
    cJSON *j = cJSON_Parse(buf);
    if (!j) { ESP_LOGW(T, "天气 JSON 解析失败"); return; }
    cJSON *cur = cJSON_GetObjectItem(j, "current");
    if (cur) {
        cJSON *t = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON *w = cJSON_GetObjectItem(cur, "weather_code");
        if (cJSON_IsNumber(t)) s_wx.temp = (float)t->valuedouble;
        if (cJSON_IsNumber(w)) s_wx.wcode = w->valueint;
        s_wx.unit = (g_cfg.unit == 'C') ? 'C' : 'F';
        s_wx.ok = true;
        ESP_LOGI(T, "天气: %.0f%c, code=%d", s_wx.temp, s_wx.unit, s_wx.wcode);
    }
    cJSON_Delete(j);
}

bool svc_weather_get(weather_t *out) {
    if (out) *out = s_wx;
    return s_wx.ok;
}
