// 开机启动页:AlgoStone 大字 + 同页圆角像素进度条。
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "ui_draw.h"
#include "splash.h"

#define SPLASH_TITLE_Y 90      // "AlgoStone" 大字(scale 4, 32px 高)
#define SPLASH_SCALE   4
#define SPLASH_STRIDE  31      // 字距(收窄,两侧留白更大、整体更紧凑)
#define PROG_Y     142         // 进度条/starting 文字的纵向位置(靠近标题)
#define PROG_H     18          // 进度条高度
#define PROG_MGN   52          // 进度条左右留白
#define PROG_CELLS 16          // 分段像素方块个数
#define PROG_GAP   3           // 方块之间的间隙

static int s_prog = 0;         // 当前进度 0..100

// 一上电即点屏:AlgoStone + 下方 "starting"
void ui_splash(void) {
    display_init();                    // 点亮背光 + 初始化(可重复调用)
    fill_screen(C_BLACK);
    ui_draw_title(SPLASH_TITLE_Y, "AlgoStone", 0xFFFF, SPLASH_SCALE, SPLASH_STRIDE);
    ui_draw_center(PROG_Y, "starting", GRAY(140), C_BLACK, 2);
}

// 圆角矩形:第 ry 行(0..h-1)左右各收进多少像素(圆角处)。直段返回 0。
static int rr_inset(int ry, int h, int r) {
    double dyp;
    if (ry < r)            dyp = (ry + 0.5) - r;          // 上圆角
    else if (ry >= h - r)  dyp = (ry + 0.5) - (h - r);    // 下圆角
    else                   return 0;                       // 中间直段
    double s = sqrt((double)r*r - dyp*dyp);
    int inset = (int)ceil(r - s - 0.5);
    return inset < 0 ? 0 : inset;
}
// 填充一个圆角矩形(逐行,每行一条 fill_rect)
static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t col) {
    for (int ry = 0; ry < h; ry++) {
        int ins = rr_inset(ry, h, r);
        if (w - 2*ins > 0) fill_rect(x + ins, y + ry, w - 2*ins, 1, col);
    }
}

// 把 "starting" 文字换成空的圆角药丸外框(同一页,不换屏)
void ui_progress_begin(void) {
    int W = display_width();
    fill_rect(0, PROG_Y - 4, W, PROG_H + 24, C_BLACK);   // 清掉 "starting" 文字
    int x = PROG_MGN, w = W - 2*PROG_MGN, r = PROG_H / 2;
    fill_round_rect(x, PROG_Y, w, PROG_H, r, GRAY(120));        // 白灰圆角外圈
    fill_round_rect(x+2, PROG_Y+2, w-4, PROG_H-4, r-2, C_BLACK);// 内部挖空(2px 边框)
    s_prog = 0;
}

// 分段像素方块:立即填充到 pct(0..100)。每格是一个方块,点亮=白,未亮=黑。
static void progress_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int W = display_width();
    int cap = PROG_H / 2;                        // 让方块避开两端的圆角
    int innerX = PROG_MGN + cap;
    int innerW = (W - 2*PROG_MGN) - 2*cap;
    int cellw = (innerW - (PROG_CELLS - 1) * PROG_GAP) / PROG_CELLS;
    int span = PROG_CELLS * cellw + (PROG_CELLS - 1) * PROG_GAP;
    int startx = innerX + (innerW - span) / 2;   // 居中
    int ch = PROG_H - 8, cy = PROG_Y + 4;        // 方块高度(上下各留 4px)
    int lit = pct * PROG_CELLS / 100;            // 点亮的方块数
    for (int i = 0; i < PROG_CELLS; i++) {
        int cx = startx + i * (cellw + PROG_GAP);
        fill_rect(cx, cy, cellw, ch, (i < lit) ? C_WHITE : C_BLACK);
    }
    s_prog = pct;
}

// 平滑推进到 target:逐格填充,step_ms 控制速度(0=瞬间)
void ui_progress_to(int target, int step_ms) {
    if (target > 100) target = 100;
    while (s_prog < target) {
        progress_set(s_prog + 1);
        if (step_ms > 0) vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
}

// 配网模式提示屏(连不上已存 Wi-Fi 时)
void ui_splash_setup_screen(void) {
    display_init();
    fill_screen(C_BLACK);
    ui_draw_center(26,  "Wi-Fi Setup", 0xFFFF, C_BLACK, 3);
    ui_draw_center(76,  "Join:", GRAY(150), C_BLACK, 2);
    ui_draw_center(98,  "Lovebox-Setup", 0xFFFF, C_BLACK, 2);
    ui_draw_center(126, "Password:", GRAY(150), C_BLACK, 2);
    ui_draw_center(148, "lovebox888", 0xFFFF, C_BLACK, 2);
    ui_draw_center(180, "Then open:", GRAY(150), C_BLACK, 2);
    ui_draw_center(202, "192.168.4.1", 0xFFFF, C_BLACK, 2);
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
