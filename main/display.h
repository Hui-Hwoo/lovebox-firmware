#pragma once
#include <stdint.h>

// RGB565 常用颜色(期望值,驱动内部会做面板补偿)
#define RGB565(r,g,b) ((uint16_t)(((r)&0xF8)<<8 | ((g)&0xFC)<<3 | ((b)&0xF8)>>3))
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_ORANGE 0xFD20
#define C_GRAY   0x8410
#define C_DKBLUE 0x0010
// 灰阶:v=0..255 亮度,生成等比 RGB565 灰色(用于主次信息分层)
#define GRAY(v) ((uint16_t)((((v)>>3)<<11) | (((v)>>2)<<5) | ((v)>>3)))

void display_init(void);
void fill_screen(uint16_t color);
void fill_rect(int x, int y, int w, int h, uint16_t color);
int  draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int  text_width(const char *s, int scale);
int  display_width(void);
int  display_height(void);
