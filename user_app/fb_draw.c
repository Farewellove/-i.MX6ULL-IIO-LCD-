/*
 * fb_draw.c — Framebuffer 绘图模块
 *
 * 直接 mmap /dev/fb0，实现像素、矩形、文字绘制。
 * 支持 16bpp RGB565 和 32bpp，颜色输入统一 0x00RRGGBB。
 */
#include "fb_draw.h"
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

/* ── 内部全局 ── */
static int              g_fb_fd    = -1;
static uint8_t         *g_fb_ptr   = NULL;
static struct fb_var_screeninfo g_vinfo;
static struct fb_fix_screeninfo g_finfo;
static uint32_t         g_fb_size  = 0;

/* ── 像素格式转换 ── */

/* 0x00RRGGBB → 本地像素值（根据 fb 实际 R/G/B/A offset/length） */
static uint32_t color_to_pixel(uint32_t c)
{
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b = (c >>  0) & 0xFF;

    if (g_vinfo.bits_per_pixel == 16) {
        return (uint32_t)(
            ((r >> (8 - g_vinfo.red.length))   << g_vinfo.red.offset)   |
            ((g >> (8 - g_vinfo.green.length)) << g_vinfo.green.offset) |
            ((b >> (8 - g_vinfo.blue.length))  << g_vinfo.blue.offset));
    }

    /* 32bpp */
    uint32_t px = 0;
    px |= ((uint32_t)r >> (8 - g_vinfo.red.length))   << g_vinfo.red.offset;
    px |= ((uint32_t)g >> (8 - g_vinfo.green.length)) << g_vinfo.green.offset;
    px |= ((uint32_t)b >> (8 - g_vinfo.blue.length))  << g_vinfo.blue.offset;

    /* alpha/transp channel: 全不透明 */
    if (g_vinfo.transp.length > 0)
        px |= (uint32_t)((1U << g_vinfo.transp.length) - 1)
              << g_vinfo.transp.offset;

    return px;
}

/* ── API ── */

int fb_init(const char *fb_path)
{
    g_fb_fd = open(fb_path, O_RDWR);
    if (g_fb_fd < 0) {
        perror("open framebuffer");
        return -1;
    }

    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        goto err;
    }
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        goto err;
    }

    if (g_vinfo.bits_per_pixel != 16 && g_vinfo.bits_per_pixel != 32) {
        fprintf(stderr, "fb_draw: unsupported bpp=%u, only 16/32 supported\n",
                g_vinfo.bits_per_pixel);
        goto err;
    }

    g_fb_size = g_finfo.smem_len;
    g_fb_ptr = mmap(NULL, g_fb_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, g_fb_fd, 0);
    if (g_fb_ptr == MAP_FAILED) {
        perror("mmap framebuffer");
        g_fb_ptr = NULL;
        goto err;
    }

    printf("[INFO] framebuffer: %s %ux%u %ubpp line_length=%u\n",
           fb_path, g_vinfo.xres, g_vinfo.yres,
           g_vinfo.bits_per_pixel, g_finfo.line_length);
    printf("[INFO] fb color offset: R:%u/%u G:%u/%u B:%u/%u A:%u/%u\n",
           g_vinfo.red.offset,   g_vinfo.red.length,
           g_vinfo.green.offset, g_vinfo.green.length,
           g_vinfo.blue.offset,  g_vinfo.blue.length,
           g_vinfo.transp.offset,g_vinfo.transp.length);
    return 0;

err:
    close(g_fb_fd);
    g_fb_fd = -1;
    return -1;
}

void fb_close(void)
{
    if (g_fb_ptr && g_fb_ptr != MAP_FAILED)
        munmap(g_fb_ptr, g_fb_size);
    if (g_fb_fd >= 0)
        close(g_fb_fd);

    g_fb_ptr   = NULL;
    g_fb_fd    = -1;
    g_fb_size  = 0;
}

int fb_width(void)  { return (int)g_vinfo.xres; }
int fb_height(void) { return (int)g_vinfo.yres; }
int fb_bpp(void)    { return (int)g_vinfo.bits_per_pixel; }

void fb_clear(uint32_t color)
{
    fb_fill_rect(0, 0, fb_width(), fb_height(), color);
}

void fb_put_pixel(int x, int y, uint32_t color)
{
    if (!g_fb_ptr || x < 0 || y < 0 ||
        x >= (int)g_vinfo.xres || y >= (int)g_vinfo.yres)
        return;

    uint32_t px = color_to_pixel(color);
    unsigned int bpp = g_vinfo.bits_per_pixel;

    if (bpp == 16) {
        uint16_t *p = (uint16_t *)g_fb_ptr;
        p[y * g_finfo.line_length / 2 + x] = (uint16_t)px;
    } else {
        uint32_t *p = (uint32_t *)g_fb_ptr;
        p[y * g_finfo.line_length / 4 + x] = px;
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!g_fb_ptr) return;

    /* 边界裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g_vinfo.xres) w = (int)g_vinfo.xres - x;
    if (y + h > (int)g_vinfo.yres) h = (int)g_vinfo.yres - y;
    if (w <= 0 || h <= 0) return;

    uint32_t px = color_to_pixel(color);
    unsigned int bpp = g_vinfo.bits_per_pixel;
    int row, col;
    unsigned int line_bytes = g_finfo.line_length;

    for (row = y; row < y + h; row++) {
        for (col = x; col < x + w; col++) {
            if (bpp == 16) {
                uint16_t *p = (uint16_t *)g_fb_ptr;
                p[row * line_bytes / 2 + col] = (uint16_t)px;
            } else {
                uint32_t *p = (uint32_t *)g_fb_ptr;
                p[row * line_bytes / 4 + col] = px;
            }
        }
    }
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    if (!g_fb_ptr) return;

    unsigned char ch = (unsigned char)c;
    if (ch < 32 || ch > 126) ch = '?';

    const unsigned char *data = font8x16[ch - 32];
    int row, col;

    for (row = 0; row < 16; row++) {
        unsigned char bits = data[row];
        for (col = 0; col < 8; col++) {
            fb_put_pixel(x + col, y + row,
                         (bits & 0x80) ? fg : bg);
            bits <<= 1;
        }
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s) {
        fb_draw_char(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

void fb_draw_printf(int x, int y, uint32_t fg, uint32_t bg,
                    const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fb_draw_string(x, y, buf, fg, bg);
}
