/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/27
 * @Description: GT911 触摸屏 input 事件调试程序
 *               直接读取 /dev/input/eventX 并逐条打印 input_event，
 *               用于验证驱动的多点触摸上报 (ABS_MT_*) 与触摸状态 (BTN_TOUCH)。
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

/*
 * 用法: ./input_test /dev/input/eventX
 *
 * 每次触摸内核上报一帧事件，典型顺序为:
 *   ABS_MT_POSITION_X → ABS_MT_POSITION_Y → ABS_MT_TOUCH_MAJOR
 *   → BTN_TOUCH=1 → ---- SYN ----
 * 所以判断一帧是否完整，要看 "---- SYN ----" 之前是否同时出现了
 * 坐标事件和 BTN_TOUCH。
 */
int main(int argc, char *argv[])
{
    int fd;
    struct input_event ev;

    if (argc != 2) {
        printf("Usage: %s /dev/input/eventX\n", argv[0]);
        return -1;
    }

    /* 阻塞方式打开，read 会一直等待直到有事件到达 */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* 事件循环: 一次 read 取出一个 struct input_event */
    while (1) {
        /* 字节数不等于结构体大小说明数据不完整，跳过继续读 */
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        if (ev.type == EV_ABS) {
            /* 绝对坐标事件: 多点触摸走 ABS_MT_*，单点兼容走 ABS_X/Y */
            switch (ev.code) {
            case ABS_MT_POSITION_X:
                printf("ABS_MT_POSITION_X = %d\n", ev.value);
                break;
            case ABS_MT_POSITION_Y:
                printf("ABS_MT_POSITION_Y = %d\n", ev.value);
                break;
            case ABS_MT_TOUCH_MAJOR:
                printf("ABS_MT_TOUCH_MAJOR = %d\n", ev.value);
                break;
            case ABS_X:
                printf("ABS_X = %d\n", ev.value);
                break;
            case ABS_Y:
                printf("ABS_Y = %d\n", ev.value);
                break;
            default:
                printf("EV_ABS code=%u value=%d\n", ev.code, ev.value);
                break;
            }
        } else if (ev.type == EV_KEY) {
            /* 按键事件: BTN_TOUCH 1=按下 0=抬起 */
            if (ev.code == BTN_TOUCH)
                printf("BTN_TOUCH = %d\n", ev.value);
            else
                printf("EV_KEY code=%u value=%d\n", ev.code, ev.value);
        } else if (ev.type == EV_SYN) {
            /* 同步事件: 标志一帧事件结束 */
            printf("---- SYN ----\n");
        }
    }

    /* 死循环不会走到这里，仅保持与其他程序一致的收尾结构 */
    close(fd);
    return 0;
}
