/*
 * fb_draw.h — Framebuffer 绘图模块头文件
 *
 * 支持 16bpp RGB565 和 32bpp (XRGB/ARGB/RGBX)。
 * 颜色统一用 0x00RRGGBB，内部按实际像素格式转换。
 */
#ifndef FB_DRAW_H
#define FB_DRAW_H

#include <stdint.h>

/* ── 颜色常量 (0x00RRGGBB) ── */
#define FB_BLACK   0x00000000
#define FB_WHITE   0x00FFFFFF
#define FB_RED     0x00FF0000
#define FB_GREEN   0x0000FF00
#define FB_BLUE    0x000000FF
#define FB_YELLOW  0x00FFFF00
#define FB_CYAN    0x0000FFFF
#define FB_MAGENTA 0x00FF00FF
#define FB_GRAY    0x00808080
#define FB_DARKBLUE 0x000000AA
#define FB_ORANGE  0x00FF8800

/* ── API ── */

int  fb_init(const char *fb_path);
void fb_close(void);

int  fb_width(void);
int  fb_height(void);
int  fb_bpp(void);

void fb_clear(uint32_t color);
void fb_put_pixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

void fb_draw_char(int x, int y, char c,
                  uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char *s,
                    uint32_t fg, uint32_t bg);
void fb_draw_printf(int x, int y,
                    uint32_t fg, uint32_t bg,
                    const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

#endif /* FB_DRAW_H */
