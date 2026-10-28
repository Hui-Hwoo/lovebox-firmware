// Lovebox 自定义固件 —— 仪表盘:大时钟 + 日期 + 天气(设备自己联网,不依赖 Mac)
// 对时: SNTP(pool.ntp.org),时区固定太平洋(自动夏令时)。
// 定位: ip-api.com (HTTP) 拿 lat/lon/city。 天气: open-meteo (HTTP)。
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "cJSON.h"
#include "display.h"
#include "netcfg.h"

static const char *T = "dash";

// 天气状态(坐标由城市名地理编码得到)
static double s_lat = 37.7749, s_lon = -122.4194;   // 回落:旧金山
static float  s_temp_f = 0;
static int    s_wcode = -1;
static bool   s_weather_ok = false;

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

// ---------- 城市名 -> 坐标(open-meteo 地理编码)----------
static void geocode(const char *city) {
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

// ---------- 天气 ----------
static void fetch_weather(void) {
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
        if (cJSON_IsNumber(t)) s_temp_f = (float)t->valuedouble;
        if (cJSON_IsNumber(w)) s_wcode = w->valueint;
        s_weather_ok = true;
        ESP_LOGI(T, "天气: %.0f%c, code=%d", s_temp_f, g_cfg.unit, s_wcode);
    }
    cJSON_Delete(j);
}

// WMO 天气代码 -> 简短天气描述(混合大小写)
static const char *wmo_text(int c) {
    if (c == 0) return "Clear";
    if (c == 1) return "Mainly Clear";
    if (c == 2) return "Partly Cloudy";
    if (c == 3) return "Overcast";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 57) return "Drizzle";
    if (c >= 61 && c <= 67) return "Rain";
    if (c >= 71 && c <= 77) return "Snow";
    if (c >= 80 && c <= 82) return "Showers";
    if (c == 85 || c == 86) return "Snow";
    if (c >= 95) return "Storm";
    return "--";
}

// ---------- SNTP ----------
static volatile bool s_time_ok = false;
static void sntp_synced(struct timeval *tv) { s_time_ok = true; ESP_LOGI(T, "SNTP 已对时"); }

static void sntp_start(void) {
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);   // 太平洋时区 + 自动夏令时
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = sntp_synced;
    esp_netif_sntp_init(&cfg);
}

// ---------- 居中画一行文字,先清一条背景带 ----------
static void draw_center(int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    int w = text_width(s, scale);
    int x = (display_width() - w) / 2;
    if (x < 0) x = 0;
    fill_rect(0, y, display_width(), 8*scale, bg);  // 清整行背景
    draw_text(x, y, s, fg, bg, scale);
}

#define BG C_BLACK

// 版面坐标(320x240)与字号/灰阶 —— 主次分层
#define Y_CLOCK  22
#define S_CLOCK  6
#define Y_DATE   92
#define S_DATE   2
#define Y_DIV    120          // 分隔线
#define Y_TEMP   138
#define S_TEMP   4
#define Y_COND   182
#define S_COND   2
#define Y_CITY   206
#define S_CITY   2
#define Y_IP     230          // 底部小字:设备 IP(找设置页用)
#define S_IP     1
#define COL_CLOCK  0xFFFF      // 时间:最亮(主信息)
#define COL_DATE   GRAY(170)   // 日期:浅灰
#define COL_DIV    GRAY(60)    // 分隔线:很暗
#define COL_TEMP   GRAY(235)   // 温度:亮(次主信息)
#define COL_COND   GRAY(150)   // 天气描述:中灰
#define COL_CITY   GRAY(105)   // 城市:暗灰(辅助)
#define COL_IP     GRAY(70)    // IP:最暗

// 画温度 "<num>°<U>":数字 + 度数小圈 + 单位字母,整体居中,先清行
static void draw_temp(int y, const char *num, int scale, uint16_t col) {
    char ul[2] = { (g_cfg.unit=='C') ? 'C' : 'F', 0 };
    int W = display_width();
    int wnum = text_width(num, scale);
    int dd = scale * 2;                    // 度圈外径
    int gap = scale;
    int wU = text_width(ul, scale);
    int total = wnum + gap + dd + gap + wU;
    int x = (W - total) / 2;
    if (x < 0) x = 0;
    fill_rect(0, y, W, 8*scale, BG);       // 清温度行
    draw_text(x, y, num, col, BG, scale);
    int dx = x + wnum + gap;
    int t = scale >= 3 ? 2 : 1;            // 圈的线宽
    fill_rect(dx, y, dd, dd, col);         // 外方块
    fill_rect(dx+t, y+t, dd-2*t, dd-2*t, BG); // 内挖空 = 圆环(度数符号)
    draw_text(dx + dd + gap, y, ul, col, BG, scale);
}

// 配网模式提示屏(连不上已存 Wi-Fi 时)
void dashboard_setup_screen(void) {
    display_init();
    fill_screen(C_BLACK);
    draw_center(40,  "Wi-Fi Setup", 0xFFFF, C_BLACK, 3);
    draw_center(95,  "1. Join Wi-Fi:", GRAY(150), C_BLACK, 2);
    draw_center(120, "Lovebox-Setup", 0xFFFF, C_BLACK, 2);
    draw_center(160, "2. Open in browser:", GRAY(150), C_BLACK, 2);
    draw_center(185, "192.168.4.1", 0xFFFF, C_BLACK, 2);
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

void dashboard_run(void) {
    display_init();
    fill_screen(BG);
    draw_center(100, "connecting...", GRAY(140), BG, 2);

    sntp_start();
    geocode(g_cfg.city);       // 城市名 -> 坐标
    fetch_weather();

    // 缓存上次渲染内容;只有变化时才重画,避免闪烁
    char cur_clock[8] = "",  last_clock[8] = "";
    char cur_date[24] = "",  last_date[24] = "";
    char cur_temp[8] = "",   last_temp[8] = "";
    char cur_cond[24] = "",  last_cond[24] = "";
    char last_city[40] = "";
    char last_ip[16] = "";
    bool div_drawn = false;
    int  last_wfetch = -1000;   // 分钟计数,用于每15分钟刷新天气

    // 等首次对时(最多30秒),期间保持提示
    for (int i = 0; i < 30 && !s_time_ok; i++) vTaskDelay(pdMS_TO_TICKS(1000));
    fill_screen(BG);  // 清掉提示语

    while (1) {
        time_t now = time(NULL);
        struct tm tm; localtime_r(&now, &tm);

        // 天气:每15分钟刷新一次
        int totmin = tm.tm_hour*60 + tm.tm_min;
        if (s_time_ok && (totmin - last_wfetch >= 15 || last_wfetch < -100)) {
            fetch_weather(); last_wfetch = totmin;
        }

        // 组装当前应显示的各项(保留原始大小写)
        strftime(cur_clock, sizeof(cur_clock), "%H:%M", &tm);
        strftime(cur_date, sizeof(cur_date), "%a, %b %e", &tm);   // 如 "Wed, Oct 28"
        if (s_weather_ok) {
            snprintf(cur_temp, sizeof(cur_temp), "%.0f", s_temp_f);
            snprintf(cur_cond, sizeof(cur_cond), "%s", wmo_text(s_wcode));
        }

        // 分隔线(静态,画一次)
        if (!div_drawn) { fill_rect(60, Y_DIV, display_width()-120, 2, COL_DIV); div_drawn = true; }

        // 时钟:逐位刷新,只重画发生变化的那一位(其余不动)
        {
            int cellw = 8*S_CLOCK + S_CLOCK;
            int x0 = (display_width() - (5*cellw - S_CLOCK)) / 2;
            if (strlen(last_clock) != 5) {
                draw_center(Y_CLOCK, cur_clock, COL_CLOCK, BG, S_CLOCK);
            } else {
                for (int i = 0; i < 5; i++) if (cur_clock[i] != last_clock[i]) {
                    char ch[2] = { cur_clock[i], 0 };
                    draw_text(x0 + i*cellw, Y_CLOCK, ch, COL_CLOCK, BG, S_CLOCK);
                }
            }
            strcpy(last_clock, cur_clock);
        }
        // 日期 / 温度 / 天气描述 / 城市 —— 各自内容变了才重画
        if (strcmp(cur_date, last_date)) { draw_center(Y_DATE, cur_date, COL_DATE, BG, S_DATE); strcpy(last_date, cur_date); }
        if (cur_temp[0] && strcmp(cur_temp, last_temp)) { draw_temp(Y_TEMP, cur_temp, S_TEMP, COL_TEMP); strcpy(last_temp, cur_temp); }
        if (cur_cond[0] && strcmp(cur_cond, last_cond)) { draw_center(Y_COND, cur_cond, COL_COND, BG, S_COND); strcpy(last_cond, cur_cond); }
        if (g_cfg.city[0] && strcmp(g_cfg.city, last_city)) { draw_center(Y_CITY, g_cfg.city, COL_CITY, BG, S_CITY); strcpy(last_city, g_cfg.city); }
        if (g_ip[0] && strcmp(g_ip, last_ip))               { draw_center(Y_IP, g_ip, COL_IP, BG, S_IP); strcpy(last_ip, g_ip); }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
