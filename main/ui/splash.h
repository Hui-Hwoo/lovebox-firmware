#pragma once

// 开机启动页:AlgoStone 品牌名 + 同页像素进度条。

void ui_splash(void);                       // 一上电即点屏:AlgoStone + "starting"
void ui_splash_setup_screen(void);          // 配网提示屏(阻塞,连不上 Wi-Fi 时用)

void ui_progress_begin(void);               // 把 "starting" 换成空的圆角进度条(同一页)
void ui_progress_to(int target, int step_ms); // 平滑推进进度到 target(0..100);step_ms=0 瞬间
