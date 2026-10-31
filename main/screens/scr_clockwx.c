// 画面:大时钟 + 日期 + 天气(温度+图标)+ 城市 + IP + 版本 + Claude 图标。
// 这是从原 dashboard 迁来的第一个画面,实现 screen_t 接口。
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "display.h"
#include "ui_draw.h"
#include "netcfg.h"
#include "cfg_store.h"
#include "svc_time.h"
#include "svc_weather.h"
#include "claude_icon.h"
#include "screen.h"

#define NS "clockwx"          // 本画面的配置命名空间
#define BG C_BLACK

// 版面坐标(320x240)与字号/灰阶 —— 主次分层
#define Y_CLOCK  20
#define S_CLOCK  6
#define Y_DATE   88
#define S_DATE   2
#define Y_DIV    116          // 分隔线
#define Y_WX     140          // 天气(温度+图标 一行)
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
    case WX_SUN:    icon_sun(cx, cy, 6, col); break;
    case WX_PARTLY: icon_sun(cx-6, cy-5, 4, col); icon_cloud(cx+3, cy+3, col); break;
    case WX_CLOUD:  icon_cloud(cx, cy, col); break;
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
    case WX_STORM:  icon_cloud(cx, cy-4, col);
                    draw_line(cx+2, cy+5, cx-3, cy+10, col);
                    draw_line(cx-3, cy+10, cx+3, cy+10, col);
                    draw_line(cx+3, cy+10, cx-2, cy+16, col); break;
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

// ---------- 画面配置(切入时从 NVS 读取)----------
static char s_city[48] = "San Francisco";
static char s_unit = 'F';

// 天气一行:温度 "<num>°<U>" + 天气图标,整体居中,固定布局(不随描述长短变化)
static void draw_weather(int y, const char *num, int wcode, int scale, uint16_t col) {
    char ul[2] = { (s_unit == 'C') ? 'C' : 'F', 0 };
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

// ---------- 画面状态(切入时重置)----------
static char last_clock[8], last_date[24], last_wx[40], last_city[40], last_ip[16];
static int  last_wfetch;

// 从本画面的 NVS 命名空间读取配置(城市 / 单位)
static void load_cfg(void) {
    char u[8];
    cfg_get(NS, "city", s_city, sizeof(s_city), "San Francisco");
    cfg_get(NS, "unit", u, sizeof(u), "F");
    s_unit = (u[0] == 'C') ? 'C' : 'F';
}

// ---------- screen_t 回调 ----------
static void clockwx_prepare(void) {
    load_cfg();
    svc_weather_geocode(s_city);       // 城市名 -> 坐标
    svc_weather_refresh(s_unit);       // 首次拉天气(切屏前备好,避免留白)
}

static void clockwx_enter(void) {
    // 重置缓存,保证首帧全量重画
    last_clock[0] = last_date[0] = last_wx[0] = last_city[0] = last_ip[0] = 0;
    last_wfetch = -1000;

    // 静态元素:分隔线 + Claude 图标 + 版本号(各画一次)
    fill_rect(60, Y_DIV, display_width()-120, 2, COL_DIV);
    draw_bitmap(ICON_X, ICON_Y, CLAUDE_ICON_W, CLAUDE_ICON_H, claude_icon_bits, C_CLAUDE, BG);
    char vs[40]; snprintf(vs, sizeof(vs), "v%s", esp_app_get_description()->version);
    draw_text(3, Y_VER, vs, COL_VER, BG, S_VER);

    // prepare() 已取过天气:记录当前分钟,避免 tick 第一拍又拉一次(造成停顿)
    weather_t wx;
    if (svc_time_ready() && svc_weather_get(&wx)) {
        time_t n = time(NULL); struct tm t; localtime_r(&n, &t);
        last_wfetch = t.tm_hour*60 + t.tm_min;
    }
}

static void clockwx_tick(void) {
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);

    // 天气:每15分钟刷新一次
    int totmin = tm.tm_hour*60 + tm.tm_min;
    if (svc_time_ready() && (totmin - last_wfetch >= 15 || last_wfetch < -100)) {
        svc_weather_refresh(s_unit); last_wfetch = totmin;
    }

    weather_t wx; svc_weather_get(&wx);

    // 组装当前应显示的各项(保留原始大小写)
    char cur_clock[8], cur_date[24], cur_temp[8] = "", cur_wx[40] = "";
    strftime(cur_clock, sizeof(cur_clock), "%H:%M", &tm);
    strftime(cur_date, sizeof(cur_date), "%a, %b %e", &tm);   // 如 "Wed, Oct 28"
    if (wx.ok) {
        snprintf(cur_temp, sizeof(cur_temp), "%.0f", wx.temp);
        snprintf(cur_wx, sizeof(cur_wx), "%s|%d", cur_temp, wx.wcode);
    }

    // 时钟:逐位刷新,只重画发生变化的那一位(其余不动)
    {
        int cellw = 8*S_CLOCK + S_CLOCK;
        int x0 = (display_width() - (5*cellw - S_CLOCK)) / 2;
        if (strlen(last_clock) != 5) {
            ui_draw_center(Y_CLOCK, cur_clock, COL_CLOCK, BG, S_CLOCK);
        } else {
            for (int i = 0; i < 5; i++) if (cur_clock[i] != last_clock[i]) {
                char ch[2] = { cur_clock[i], 0 };
                draw_text(x0 + i*cellw, Y_CLOCK, ch, COL_CLOCK, BG, S_CLOCK);
            }
        }
        strcpy(last_clock, cur_clock);
    }
    // 日期 / 天气(一行) / 城市 / IP —— 各自内容变了才重画
    if (strcmp(cur_date, last_date)) { ui_draw_center(Y_DATE, cur_date, COL_DATE, BG, S_DATE); strcpy(last_date, cur_date); }
    if (cur_wx[0] && strcmp(cur_wx, last_wx)) { draw_weather(Y_WX, cur_temp, wx.wcode, S_WX, COL_WX); strcpy(last_wx, cur_wx); }
    if (s_city[0] && strcmp(s_city, last_city)) { ui_draw_center(Y_CITY, s_city, COL_CITY, BG, S_CITY); strcpy(last_city, s_city); }
    if (g_ip[0] && strcmp(g_ip, last_ip))               { draw_left_line(Y_IP, g_ip, COL_IP, S_IP, IP_CLEAR_W); strcpy(last_ip, g_ip); }
}

static const scr_field_t clockwx_fields[] = {
    { .key = "city", .label = "城市(英文名)", .type = FLD_TEXT,   .def = "San Francisco" },
    { .key = "unit", .label = "温度单位",       .type = FLD_SELECT, .def = "F", .options = "F:华氏 F,C:摄氏 C" },
};

const screen_t scr_clockwx = {
    .id       = "clockwx",
    .name     = "时钟+天气",
    .fields   = clockwx_fields,
    .n_fields = sizeof(clockwx_fields)/sizeof(clockwx_fields[0]),
    .prepare  = clockwx_prepare,
    .enter    = clockwx_enter,
    .tick     = clockwx_tick,
    .leave    = NULL,
};
