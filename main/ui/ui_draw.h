#pragma once
#include <stdint.h>

// 多个画面/启动页共用的绘制助手(基于 display.h 的底层原语)。

// 居中画一行文字,先清一条整行背景带再画。
void ui_draw_center(int y, const char *s, uint16_t fg, uint16_t bg, int scale);

// 居中画标题,字距 stride 可调(stride < 字宽时更紧凑,能放下更大的字号)。
void ui_draw_title(int y, const char *s, uint16_t fg, int scale, int stride);
