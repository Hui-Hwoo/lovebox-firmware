// 画面:超大时钟 —— 全屏大号 HH:MM + 日期 + 秒,不依赖网络(仅用时间)。
// 用来演示多画面热切换;也是一个最简画面的范例(prepare/leave 都为 NULL)。
#include <string.h>
#include <time.h>
#include "display.h"
#include "ui_draw.h"
#include "svc_time.h"
#include "screen.h"

#define BG       C_BLACK
#define Y_TIME   72           // 大时钟 HH:MM
#define S_TIME   7            // 字号(56px 高)
#define Y_DATE   150
#define S_DATE   2
#define Y_SEC    182          // 秒(带动感,表明在走)
#define S_SEC    3
#define COL_TIME 0xFFFF
#define COL_DATE GRAY(150)
#define COL_SEC  GRAY(90)

static char last_hm[8], last_date[24], last_sec[4];

static void bigclock_enter(void) {
    last_hm[0] = last_date[0] = last_sec[0] = 0;   // 强制首帧全量重画
    if (!svc_time_ready()) ui_draw_center(Y_TIME, "--:--", COL_TIME, BG, S_TIME);
}

static void bigclock_tick(void) {
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);

    char hm[8], date[24], sec[4];
    strftime(hm,   sizeof(hm),   "%H:%M", &tm);
    strftime(date, sizeof(date), "%a, %b %e", &tm);
    strftime(sec,  sizeof(sec),  "%S", &tm);

    // 大时钟:逐位刷新,只重画变化的那一位(避免整屏闪烁)
    int cellw = 8*S_TIME + S_TIME;
    int x0 = (display_width() - (5*cellw - S_TIME)) / 2;
    if (strlen(last_hm) != 5) {
        ui_draw_center(Y_TIME, hm, COL_TIME, BG, S_TIME);
    } else {
        for (int i = 0; i < 5; i++) if (hm[i] != last_hm[i]) {
            char ch[2] = { hm[i], 0 };
            draw_text(x0 + i*cellw, Y_TIME, ch, COL_TIME, BG, S_TIME);
        }
    }
    strcpy(last_hm, hm);

    if (strcmp(date, last_date)) { ui_draw_center(Y_DATE, date, COL_DATE, BG, S_DATE); strcpy(last_date, date); }
    if (strcmp(sec,  last_sec))  { ui_draw_center(Y_SEC,  sec,  COL_SEC,  BG, S_SEC);  strcpy(last_sec,  sec);  }
}

const screen_t scr_bigclock = {
    .id      = "bigclock",
    .name    = "超大时钟",
    .prepare = NULL,
    .enter   = bigclock_enter,
    .tick    = bigclock_tick,
    .leave   = NULL,
};
