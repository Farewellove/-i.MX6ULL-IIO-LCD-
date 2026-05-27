/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/14
 * @Description: LCD 显示测试程序 - 通过 framebuffer 显示纯色
 * @Copyright: Copyright (©) 2026 Farewellove. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    unsigned char *fb;
    unsigned int screensize;
    unsigned int x, y;
    unsigned int color;

    /* 1. 打开 framebuffer 设备 */
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
    {
        perror("open /dev/fb0");
        return -1;
    }

    /* 2. 获取屏幕信息 */
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo))
    {
        perror("ioctl FBIOGET_VSCREENINFO");
        close(fd);
        return -1;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo))
    {
        perror("ioctl FBIOGET_FSCREENINFO");
        close(fd);
        return -1;
    }

    printf("LCD: %dx%d, bpp=%d, line_length=%d\n",
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length);

    screensize = finfo.line_length * vinfo.yres;

    /* 3. mmap framebuffer */
    fb = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return -1;
    }

    /* 4. 填充颜色 */
    color = 0x0000FF00; /* 默认绿色 (RGB565: R=0, G=255, B=0 -> 0x07E0) */

    if (argc > 1)
    {
        if (!strcmp(argv[1], "red"))
            color = 0x000000FF; /* 红色 */
        else if (!strcmp(argv[1], "green"))
            color = 0x0000FF00; /* 绿色 */
        else if (!strcmp(argv[1], "blue"))
            color = 0x00FF0000; /* 蓝色 */
        else if (!strcmp(argv[1], "white"))
            color = 0x00FFFFFF; /* 白色 */
        else if (!strcmp(argv[1], "black"))
            color = 0x00000000; /* 黑色 */
        else if (!strcmp(argv[1], "test"))
            color = 0xFFFFFFFF; /* 测试: 全白 */
    }

    printf("Filling screen with color: 0x%08X\n", color);
    printf("  red: offset=%d length=%d, green: offset=%d length=%d, blue: offset=%d length=%d\n",
           vinfo.red.offset, vinfo.red.length,
           vinfo.green.offset, vinfo.green.length,
           vinfo.blue.offset, vinfo.blue.length);

    if (vinfo.bits_per_pixel == 16)
    {
        unsigned short *p = (unsigned short *)fb;
        /* 根据 framebuffer 实际的 R/G/B 位偏移构造像素值 */
        unsigned short c16 = ((color & 0xFF) >> (8 - vinfo.red.length)) << vinfo.red.offset |
                             ((color & 0xFF00) >> (16 - vinfo.green.length)) << vinfo.green.offset |
                             ((color & 0xFF0000) >> (24 - vinfo.blue.length)) << vinfo.blue.offset;
        for (y = 0; y < vinfo.yres; y++)
            for (x = 0; x < vinfo.xres; x++)
                p[y * finfo.line_length / 2 + x] = c16;
    }
    else if (vinfo.bits_per_pixel == 32)
    {
        unsigned int *p = (unsigned int *)fb;
        /* 根据 framebuffer 实际的 R/G/B 位偏移构造像素值 */
        unsigned int c32 = (((color >>  0) & 0xFF) >> (8 - vinfo.red.length)) << vinfo.red.offset |
                           (((color >>  8) & 0xFF) >> (8 - vinfo.green.length)) << vinfo.green.offset |
                           (((color >> 16) & 0xFF) >> (8 - vinfo.blue.length)) << vinfo.blue.offset;
        for (y = 0; y < vinfo.yres; y++)
            for (x = 0; x < vinfo.xres; x++)
                p[y * finfo.line_length / 4 + x] = c32;
    }

    printf("Done. Press Ctrl+C to exit.\n");

    /* 5. 保持显示 */
    while (1)
    {
        sleep(1);
    }

    munmap(fb, screensize);
    close(fd);
    return 0;
}
