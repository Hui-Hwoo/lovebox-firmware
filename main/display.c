// Lovebox 自定义固件 —— 显示驱动(ILI9341 320x240, 已确认引脚 + 颜色补偿)
// 引脚: SCLK18 MOSI23 CS5 DC26 RST27 BL16; SPI2(VSPI) 10MHz; MADCTL=0x28(MV+BGR)+INVON。
// 颜色补偿: 面板变换 seenR=~sentB, seenG=sentG, seenB=~sentR。发送前对每个颜色做 comp()。
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "display.h"
#include "font8x8.h"

#define PIN_BL 16
#define PIN_SCLK 18
#define PIN_MOSI 23
#define PIN_CS 5
#define PIN_DC 26
#define PIN_RST 27
#define DISP_W 320
#define DISP_H 240

static const char *T = "disp";
static spi_device_handle_t DEV;

// 颜色:直通(用 MADCTL=RGB + INVOFF,所见即所得,无需软件补偿)
static inline uint16_t comp(uint16_t d) { return d; }

static void wr(int dc, const uint8_t *b, int n) {
    gpio_set_level(PIN_DC, dc);
    spi_transaction_t t = { .length = (size_t)n * 8, .tx_buffer = b };
    spi_device_polling_transmit(DEV, &t);
}
static void CMD(uint8_t c) { wr(0, &c, 1); }
static void DAT1(uint8_t v) { wr(1, &v, 1); }
static void DATN(const uint8_t *b, int n) { wr(1, b, n); }

// 设定写入窗口 [x,x+w-1] x [y,y+h-1]
static void set_window(int x, int y, int w, int h) {
    int x1 = x + w - 1, y1 = y + h - 1;
    uint8_t a[4];
    CMD(0x2A); a[0]=x>>8; a[1]=x&0xFF; a[2]=x1>>8; a[3]=x1&0xFF; DATN(a,4);
    CMD(0x2B); a[0]=y>>8; a[1]=y&0xFF; a[2]=y1>>8; a[3]=y1&0xFF; DATN(a,4);
    CMD(0x2C);
}

void display_init(void) {
    gpio_reset_pin(PIN_BL);  gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);  gpio_set_level(PIN_BL, 1);
    gpio_reset_pin(PIN_DC);  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_RST); gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    spi_bus_config_t bus = { .mosi_io_num=PIN_MOSI, .miso_io_num=-1, .sclk_io_num=PIN_SCLK,
                             .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=4096 };
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    spi_device_interface_config_t dev = { .clock_speed_hz=10*1000*1000, .mode=0, .spics_io_num=PIN_CS, .queue_size=7 };
    spi_bus_add_device(SPI2_HOST, &dev, &DEV);
    gpio_set_level(PIN_RST,0); vTaskDelay(pdMS_TO_TICKS(40));
    gpio_set_level(PIN_RST,1); vTaskDelay(pdMS_TO_TICKS(150));
    CMD(0x11); vTaskDelay(pdMS_TO_TICKS(130));       // SLPOUT
    CMD(0x3A); DAT1(0x55);                            // COLMOD 16bpp
    CMD(0x36); DAT1(0x60);                            // MADCTL MV+MX (横屏320x240, RGB)
    CMD(0x21);                                        // INVON(本面板需反相,黑底才是黑)
    CMD(0x29); vTaskDelay(pdMS_TO_TICKS(20));         // DISPON
    ESP_LOGI(T, "display init done");
}

// 用一行缓冲填充矩形(颜色已补偿)
void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x<0||y<0||w<=0||h<=0||x+w>DISP_W||y+h>DISP_H) return;
    uint16_t c = comp(color);
    static uint8_t line[DISP_W*2];
    for (int i=0;i<w;i++){ line[2*i]=c>>8; line[2*i+1]=c&0xFF; }
    set_window(x,y,w,h);
    for (int r=0;r<h;r++) DATN(line, w*2);
}

void fill_screen(uint16_t color) { fill_rect(0,0,DISP_W,DISP_H,color); }

// 画一个放大 scale 倍的 8x8 字符。fg/bg 为期望颜色;transparent!=0 时不画背景。
static void draw_char(int x, int y, char ch, uint16_t fg, uint16_t bg, int scale) {
    unsigned char uc = (unsigned char)ch;
    if (uc >= 128) uc = '?';
    const char *g = font8x8_basic[uc];
    int cw = 8*scale, chh = 8*scale;
    if (x<0||y<0||x+cw>DISP_W||y+chh>DISP_H) return;
    uint16_t cf = comp(fg), cb = comp(bg);
    static uint8_t line[DISP_W*2];
    set_window(x, y, cw, chh);
    for (int row=0; row<8; row++) {
        uint8_t bits = (uint8_t)g[row];
        // 先把这一字形行展开成 cw 宽的一像素行
        for (int col=0; col<8; col++) {
            uint16_t v = ((bits>>col)&1) ? cf : cb;
            for (int s=0;s<scale;s++){ int px=(col*scale+s)*2; line[px]=v>>8; line[px+1]=v&0xFF; }
        }
        // 纵向重复 scale 行
        for (int s=0;s<scale;s++) DATN(line, cw*2);
    }
}

// 画字符串;返回结束 x。字距 = 1*scale 像素。
int draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    int cx = x;
    for (; *s; s++) {
        draw_char(cx, y, *s, fg, bg, scale);
        cx += 8*scale + scale;   // 字宽 + 1 像素字距(放大后)
    }
    return cx;
}

int text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n>0 ? n*(8*scale+scale) - scale : 0;
}

// 画 1px 直线(Bresenham)
void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1>x0 ? x1-x0 : x0-x1, sx = x0<x1 ? 1 : -1;
    int dy = y1>y0 ? y0-y1 : y1-y0, sy = y0<y1 ? 1 : -1;   // dy 为负
    int err = dx + dy, e2;
    while (1) {
        fill_rect(x0, y0, 1, 1, color);
        if (x0==x1 && y0==y1) break;
        e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// 画 1bpp 位图(行优先,每行 (w+7)/8 字节,MSB 优先):1=fg,0=bg
void draw_bitmap(int x, int y, int w, int h, const uint8_t *bits, uint16_t fg, uint16_t bg) {
    if (x<0||y<0||x+w>DISP_W||y+h>DISP_H) return;
    int stride = (w+7)/8;
    uint16_t cf = comp(fg), cb = comp(bg);
    static uint8_t line[DISP_W*2];
    set_window(x, y, w, h);
    for (int r=0; r<h; r++) {
        const uint8_t *row = bits + r*stride;
        for (int c=0; c<w; c++) {
            uint16_t v = ((row[c>>3] >> (7-(c&7))) & 1) ? cf : cb;
            line[2*c] = v>>8; line[2*c+1] = v&0xFF;
        }
        DATN(line, w*2);
    }
}

int display_width(void)  { return DISP_W; }
int display_height(void) { return DISP_H; }
