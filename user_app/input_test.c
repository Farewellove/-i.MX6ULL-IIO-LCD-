#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(int argc, char *argv[])
{
    int fd;
    struct input_event ev;

    if (argc != 2) {
        printf("Usage: %s /dev/input/eventX\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    while (1) {
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        if (ev.type == EV_ABS) {
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
            if (ev.code == BTN_TOUCH)
                printf("BTN_TOUCH = %d\n", ev.value);
            else
                printf("EV_KEY code=%u value=%d\n", ev.code, ev.value);
        } else if (ev.type == EV_SYN) {
            printf("---- SYN ----\n");
        }
    }

    close(fd);
    return 0;
}
