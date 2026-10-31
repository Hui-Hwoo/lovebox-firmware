#include <string.h>
#include "display.h"
#include "ui_draw.h"

void ui_draw_center(int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    int w = text_width(s, scale);
    int x = (display_width() - w) / 2;
    if (x < 0) x = 0;
    fill_rect(0, y, display_width(), 8*scale, bg);  // 清整行背景
    draw_text(x, y, s, fg, bg, scale);
}

void ui_draw_title(int y, const char *s, uint16_t fg, int scale, int stride) {
    int n = (int)strlen(s);
    int span = (n > 0) ? (n - 1) * stride + 8*scale : 0;
    int x = (display_width() - span) / 2;
    if (x < 0) x = 0;
    for (int i = 0; i < n; i++) {
        char c[2] = { s[i], 0 };
        draw_text(x + i*stride, y, c, fg, C_BLACK, scale);
    }
}
