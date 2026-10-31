// Lovebox 自定义固件 —— 仪表盘:大时钟 + 日期 + 天气(设备自己联网,不依赖 Mac)
// 对时: SNTP(pool.ntp.org),时区固定太平洋(自动夏令时)。
// 定位: ip-api.com (HTTP) 拿 lat/lon/city。 天气: open-meteo (HTTP)。
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "display.h"
#include "netcfg.h"
#include "claude_icon.h"

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
#define Y_CLOCK  20
#define S_CLOCK  6
#define Y_DATE   88
#define S_DATE   2
#define Y_DIV    116          // 分隔线
#define Y_WX     140          // 天气(温度+描述 一行)
#define S_WX     3
#define Y_CITY   180
#define S_CITY   2
#define Y_IP     206          // 设备 IP(只占左侧,避开右下角图标)
#define S_IP     1
#define Y_VER    226          // 版本号(左下角常驻)
#define S_VER    1
#define COL_VER  GRAY(95)
#define IP_CLEAR_W 266        // IP 行清除宽度(右侧留给 Claude 图标)
#define ICON_X   (320 - CLAUDE_ICON_W - 2)   // 右下角
#define ICON_Y   (240 - CLAUDE_ICON_H - 2)
#define COL_CLOCK  0xFFFF      // 时间:最亮(主信息)
#define COL_DATE   GRAY(170)   // 日期:浅灰
#define COL_DIV    GRAY(60)    // 分隔线:很暗
#define COL_WX     GRAY(225)   // 天气:亮(次主信息)
#define COL_CITY   GRAY(110)   // 城市:暗灰(辅助)
#define COL_IP     GRAY(70)    // IP:最暗

// ---------- 天气图标(用图形画,替代会溢出的文字)----------
typedef enum { WX_SUN, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM } wx_t;

static void icon_cloud(int cx, int cy, uint16_t col) {
    fill_circle(cx-7, cy+1, 5, col);
    fill_circle(cx+7, cy+1, 5, col);
    fill_circle(cx,   cy-3, 7, col);
    fill_rect(cx-7, cy+1, 15, 6, col);
}
static void icon_sun(int cx, int cy, int r, uint16_t col) {
    fill_circle(cx, cy, r, col);
    for (int k = 0; k < 8; k++) {
        float a = k * 0.78539816f;
        draw_line(cx+(int)((r+2)*cosf(a)), cy+(int)((r+2)*sinf(a)),
                  cx+(int)((r+6)*cosf(a)), cy+(int)((r+6)*sinf(a)), col);
    }
}
static void draw_weather_icon(int cx, int cy, wx_t cat, uint16_t col) {
    switch (cat) {
    case WX_SUN:
        icon_sun(cx, cy, 6, col);
        break;
    case WX_PARTLY:
        icon_sun(cx-6, cy-5, 4, col);
        icon_cloud(cx+3, cy+3, col);
        break;
    case WX_CLOUD:
        icon_cloud(cx, cy, col);
        break;
    case WX_FOG:
        for (int i = 0; i < 4; i++) { fill_rect(cx-10+(i&1)*3, cy-7+i*4, 20, 2, col); }
        break;
    case WX_RAIN:
        icon_cloud(cx, cy-4, col);
        for (int k = -1; k <= 1; k++) { draw_line(cx+k*6, cy+6, cx+k*6-2, cy+13, col); }
        break;
    case WX_SNOW:
        icon_cloud(cx, cy-4, col);
        for (int k = -1; k <= 1; k++) { fill_circle(cx+k*6, cy+10, 1, col); }
        break;
    case WX_STORM:
        icon_cloud(cx, cy-4, col);
        draw_line(cx+2, cy+5, cx-3, cy+10, col);
        draw_line(cx-3, cy+10, cx+3, cy+10, col);
        draw_line(cx+3, cy+10, cx-2, cy+16, col);
        break;
    }
}
static wx_t wmo_icon(int c) {
    if (c <= 1) return WX_SUN;
    if (c == 2) return WX_PARTLY;
    if (c == 3) return WX_CLOUD;
    if (c == 45 || c == 48) return WX_FOG;
    if (c >= 71 && c <= 77) return WX_SNOW;
    if (c == 85 || c == 86) return WX_SNOW;
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return WX_RAIN;
    if (c >= 95) return WX_STORM;
    return WX_CLOUD;
}

// 天气一行:温度 "<num>°<U>" + 天气图标,整体居中,固定布局(不随描述长短变化)
static void draw_weather(int y, const char *num, int wcode, int scale, uint16_t col) {
    char ul[2] = { (g_cfg.unit == 'C') ? 'C' : 'F', 0 };
    int W = display_width();
    int gap = scale, dd = scale * 2;
    int wnum = text_width(num, scale);
    int wU = text_width(ul, scale);
    int tempw = wnum + gap + dd + gap + wU;
    int iconw = 34, sp = 18;
    int total = tempw + sp + iconw;
    int x = (W - total) / 2;
    if (x < 0) x = 0;
    fill_rect(0, y-6, W, 8*scale + 18, BG);     // 清行(含图标上下的射线/雨滴)
    draw_text(x, y, num, col, BG, scale);
    int dx = x + wnum + gap, t = scale >= 3 ? 2 : 1;
    fill_rect(dx, y, dd, dd, col);              // 度数圆环
    fill_rect(dx+t, y+t, dd-2*t, dd-2*t, BG);
    draw_text(dx + dd + gap, y, ul, col, BG, scale);
    draw_weather_icon(x + tempw + sp + iconw/2, y + 4*scale - 2, wmo_icon(wcode), col);
}

// 居中画一行小字,但只清除左侧宽度(避开右下角图标)
static void draw_left_line(int y, const char *s, uint16_t fg, int scale, int clear_w) {
    int w = text_width(s, scale);
    int x = (display_width() - w) / 2; if (x < 0) x = 0;
    fill_rect(0, y, clear_w, 8*scale, BG);
    draw_text(x, y, s, fg, BG, scale);
}

// ---------- 启动页:品牌名 + 进度条 ----------
#define SPLASH_TITLE_Y 80      // "AlgoStone" 大字(scale 3, 24px 高)
#define PROG_Y     150         // 进度条/starting 文字的纵向位置
#define PROG_H     16          // 进度条高度
#define PROG_MGN   50          // 进度条左右留白
static int s_prog = 0;         // 当前进度 0..100

// 开机即点屏的启动画面(在连 Wi-Fi 之前调用,避免黑屏等待)
// 初始:AlgoStone + 下方 "starting"
void dashboard_splash(void) {
    display_init();                    // 点亮背光 + 初始化(可重复调用)
    fill_screen(C_BLACK);
    draw_center(SPLASH_TITLE_Y, "AlgoStone", 0xFFFF, C_BLACK, 3);
    draw_center(PROG_Y, "starting", GRAY(140), C_BLACK, 2);
}

// 把 "starting" 文字换成空进度条外框(同一页,不换屏)
void dashboard_progress_begin(void) {
    int W = display_width();
    fill_rect(0, PROG_Y - 4, W, PROG_H + 24, C_BLACK);   // 清掉 "starting" 文字
    int x = PROG_MGN, w = W - 2*PROG_MGN;
    fill_rect(x, PROG_Y, w, PROG_H, GRAY(70));            // 外框(暗)
    fill_rect(x + 2, PROG_Y + 2, w - 4, PROG_H - 4, C_BLACK); // 内部挖空
    s_prog = 0;
}

// 把进度条立即填充到 pct(0..100)
static void progress_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int W = display_width();
    int x = PROG_MGN + 2, fullw = W - 2*PROG_MGN - 4;
    int w = fullw * pct / 100;
    if (w > 0) fill_rect(x, PROG_Y + 2, w, PROG_H - 4, C_CLAUDE);  // 品牌橙填充
    s_prog = pct;
}

// 平滑推进到 target:逐格填充,step_ms 控制速度(0=瞬间)
void dashboard_progress_to(int target, int step_ms) {
    if (target > 100) target = 100;
    while (s_prog < target) {
        progress_set(s_prog + 1);
        if (step_ms > 0) vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
}

// 配网模式提示屏(连不上已存 Wi-Fi 时)
void dashboard_setup_screen(void) {
    display_init();
    fill_screen(C_BLACK);
    draw_center(26,  "Wi-Fi Setup", 0xFFFF, C_BLACK, 3);
    draw_center(76,  "Join:", GRAY(150), C_BLACK, 2);
    draw_center(98,  "Lovebox-Setup", 0xFFFF, C_BLACK, 2);
    draw_center(126, "Password:", GRAY(150), C_BLACK, 2);
    draw_center(148, "lovebox888", 0xFFFF, C_BLACK, 2);
    draw_center(180, "Then open:", GRAY(150), C_BLACK, 2);
    draw_center(202, "192.168.4.1", 0xFFFF, C_BLACK, 2);
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

void dashboard_run(void) {
    display_init();
    // 不换屏:继续启动页上的进度条(Wi-Fi 阶段已推到 ~55%)
    dashboard_progress_to(60, 6);

    sntp_start();
    dashboard_progress_to(70, 6);
    geocode(g_cfg.city);       // 城市名 -> 坐标
    dashboard_progress_to(80, 6);
    fetch_weather();
    dashboard_progress_to(90, 6);

    // 缓存上次渲染内容;只有变化时才重画,避免闪烁
    char cur_clock[8] = "",  last_clock[8] = "";
    char cur_date[24] = "",  last_date[24] = "";
    char cur_temp[8] = "",   cur_cond[24] = "";
    char cur_wx[40] = "",    last_wx[40] = "";
    char last_city[40] = "";
    char last_ip[16] = "";
    bool div_drawn = false, icon_drawn = false, ver_drawn = false;
    int  last_wfetch = -1000;   // 分钟计数,用于每15分钟刷新天气

    // 等首次对时(最多30秒),进度条在 90~99% 间缓慢爬动
    for (int i = 0; i < 30 && !s_time_ok; i++) {
        dashboard_progress_to(90 + (i * 9) / 30, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    dashboard_progress_to(100, 4);     // 填满
    vTaskDelay(pdMS_TO_TICKS(300));    // 让 100% 停留一瞬
    fill_screen(BG);  // 切换到仪表盘

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
            snprintf(cur_wx, sizeof(cur_wx), "%s|%s", cur_temp, cur_cond);
        }

        // 分隔线 + Claude 图标(静态,各画一次)
        if (!div_drawn) { fill_rect(60, Y_DIV, display_width()-120, 2, COL_DIV); div_drawn = true; }
        if (!icon_drawn) { draw_bitmap(ICON_X, ICON_Y, CLAUDE_ICON_W, CLAUDE_ICON_H, claude_icon_bits, C_CLAUDE, BG); icon_drawn = true; }
        if (!ver_drawn)  { char vs[40]; snprintf(vs, sizeof(vs), "v%s", esp_app_get_description()->version);
                           draw_text(3, Y_VER, vs, COL_VER, BG, S_VER); ver_drawn = true; }

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
        // 日期 / 天气(一行) / 城市 —— 各自内容变了才重画
        if (strcmp(cur_date, last_date)) { draw_center(Y_DATE, cur_date, COL_DATE, BG, S_DATE); strcpy(last_date, cur_date); }
        if (cur_wx[0] && strcmp(cur_wx, last_wx)) { draw_weather(Y_WX, cur_temp, s_wcode, S_WX, COL_WX); strcpy(last_wx, cur_wx); }
        if (g_cfg.city[0] && strcmp(g_cfg.city, last_city)) { draw_center(Y_CITY, g_cfg.city, COL_CITY, BG, S_CITY); strcpy(last_city, g_cfg.city); }
        if (g_ip[0] && strcmp(g_ip, last_ip))               { draw_left_line(Y_IP, g_ip, COL_IP, S_IP, IP_CLEAR_W); strcpy(last_ip, g_ip); }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
