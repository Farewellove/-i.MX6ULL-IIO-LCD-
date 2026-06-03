/**
 * touch_sensor_trigger.c
 *
 * Linux 驱动综合学习 demo —— 用户态多传感器联动 + 屏幕显示
 *
 * GT911 触摸屏 input event 触发
 *     ↓
 * 用户态阻塞监听 /dev/input/eventX
 *     ↓
 * 检测到触摸按下 (BTN_TOUCH=1 或 ABS_MT 事件)
 *     ↓
 * 通过 IIO sysfs 读取 AP3216C + ICM20608 传感器数据
 *     ↓
 * 输出到 stdout 和 / LCD 屏幕 (--tty /dev/tty1)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <time.h>

#include <linux/input.h>

/*
 * 兼容无 BTN_TOUCH 的驱动：设为 1 时 ABS_MT 事件也能触发。
 * 当前 GT911 已稳定上报 BTN_TOUCH，先关闭 ABS 兜底避免重复刷新。
 */
#define TRIGGER_BY_ABS_IF_NO_BTN  0

#define IIO_BASE  "/sys/bus/iio/devices"
#define MAX_PATH  512

/* ── 全局 TTY 输出 ── */
static FILE *g_tty_fp   = NULL;
static const char *g_event_dev = NULL;

/* ── 屏幕输出 ── */

static FILE *display_open_tty(const char *tty_path)
{
    FILE *fp = fopen(tty_path, "w");
    if (!fp) {
        fprintf(stderr, "[WARN] cannot open %s: %s, "
                "screen output disabled\n", tty_path, strerror(errno));
    }
    return fp;
}

static void display_close(void)
{
    if (g_tty_fp) {
        fclose(g_tty_fp);
        g_tty_fp = NULL;
    }
}

static void display_clear(void)
{
    if (g_tty_fp) {
        fprintf(g_tty_fp, "\033[2J\033[H");
        fflush(g_tty_fp);
    }
}

__attribute__((format(printf, 1, 2)))
static void screen_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (g_tty_fp) {
        va_start(ap, fmt);
        vfprintf(g_tty_fp, fmt, ap);
        va_end(ap);
        fflush(g_tty_fp);
    }
}

/* ── 工具函数 ── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s /dev/input/eventX [--tty /dev/tty1]\n"
        "\n"
        "Examples:\n"
        "  %s /dev/input/event1\n"
        "  sudo %s /dev/input/event1 --tty /dev/tty1\n",
        prog, prog, prog);
}

/* 单调时钟毫秒时间戳，用于防抖 */
static long long get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 大小写不敏感字符串比较 */
static int is_name_match(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* 读取 sysfs 文件内容到整数，失败返回非 0 */
static int read_sysfs_int(const char *path, int *val)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char buf[32] = {0};
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *val = atoi(buf);
    return 0;
}

static int find_iio_device_by_name(const char *target_name,
                                   char *out_path, size_t out_size)
{
    DIR *dir = opendir(IIO_BASE);
    if (!dir) {
        perror("opendir " IIO_BASE);
        return -1;
    }

    struct dirent *entry;
    int found = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0)
            continue;

        char name_path[MAX_PATH];
        snprintf(name_path, sizeof(name_path),
                 IIO_BASE "/%s/name", entry->d_name);

        FILE *fp = fopen(name_path, "r");
        if (!fp)
            continue;

        char name[64] = {0};
        if (!fgets(name, sizeof(name), fp)) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        name[strcspn(name, "\r\n")] = '\0';

        if (is_name_match(name, target_name)) {
            snprintf(out_path, out_size,
                     IIO_BASE "/%s/", entry->d_name);
            found = 0;
            break;
        }
    }
    closedir(dir);
    return found;
}

/* ── 传感器数据 ── */

static void display_ap3216c_data(const char *base_path)
{
    int raw, scale;
    char path[MAX_PATH];

    screen_printf("[AP3216C]\n");

    snprintf(path, sizeof(path), "%s/in_illuminance_raw", base_path);
    int als_ok = (read_sysfs_int(path, &raw) == 0);
    snprintf(path, sizeof(path), "%s/in_illuminance_scale", base_path);
    int als_s_ok = (read_sysfs_int(path, &scale) == 0);
    if (als_ok && als_s_ok)
        screen_printf("  ALS = %d raw  →  %d lux\n", raw, raw * scale);
    else
        screen_printf("  ALS = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_intensity_ir_raw", base_path);
    int ir_ok = (read_sysfs_int(path, &raw) == 0);
    snprintf(path, sizeof(path), "%s/in_intensity_ir_scale", base_path);
    int ir_s_ok = (read_sysfs_int(path, &scale) == 0);
    if (ir_ok && ir_s_ok)
        screen_printf("  IR  = %d raw  →  %d\n", raw, raw * scale);
    else
        screen_printf("  IR  = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_proximity_raw", base_path);
    int ps_ok = (read_sysfs_int(path, &raw) == 0);
    snprintf(path, sizeof(path), "%s/in_proximity_scale", base_path);
    int ps_s_ok = (read_sysfs_int(path, &scale) == 0);
    if (ps_ok && ps_s_ok)
        screen_printf("  PS  = %d raw  →  %d\n", raw, raw * scale);
    else
        screen_printf("  PS  = [WARN] failed to read\n");

    screen_printf("\n");
}

static void display_icm20608_data(const char *base_path)
{
    int val;
    char path[MAX_PATH];

    screen_printf("[ICM20608]\n");

    snprintf(path, sizeof(path), "%s/in_accel0_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Accel X = %6d raw  →  %+.2f g\n", val, val / 2048.0f);
    else
        screen_printf("  Accel X = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_accel1_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Accel Y = %6d raw  →  %+.2f g\n", val, val / 2048.0f);
    else
        screen_printf("  Accel Y = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_accel2_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Accel Z = %6d raw  →  %+.2f g\n", val, val / 2048.0f);
    else
        screen_printf("  Accel Z = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_anglvel0_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Gyro  X = %6d raw  →  %+.2f °/s\n", val, val / 16.4f);
    else
        screen_printf("  Gyro  X = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_anglvel1_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Gyro  Y = %6d raw  →  %+.2f °/s\n", val, val / 16.4f);
    else
        screen_printf("  Gyro  Y = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_anglvel2_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Gyro  Z = %6d raw  →  %+.2f °/s\n", val, val / 16.4f);
    else
        screen_printf("  Gyro  Z = [WARN] failed to read\n");

    snprintf(path, sizeof(path), "%s/in_temp0_raw", base_path);
    if (read_sysfs_int(path, &val) == 0)
        screen_printf("  Temp    = %6d raw  →  %.2f °C\n", val, val / 326.8f + 25.0f);
    else
        screen_printf("  Temp    = [WARN] failed to read\n");

    screen_printf("\n");
}

static void display_all_sensor_data(const char *ap_path, const char *icm_path)
{
    if (g_tty_fp)
        display_clear();

    screen_printf("========================================\n");
    screen_printf(" Touch-triggered Sensor Acquisition\n");
    screen_printf("========================================\n");
    screen_printf("\n");

    screen_printf("[GT911]\n");
    screen_printf("  Touch detected\n\n");

    if (ap_path[0] != '\0')
        display_ap3216c_data(ap_path);
    else
        screen_printf("[AP3216C] not found\n\n");

    if (icm_path[0] != '\0')
        display_icm20608_data(icm_path);
    else
        screen_printf("[ICM20608] not found\n\n");

    screen_printf("----------------------------------------\n");
    screen_printf("Event device : %s\n",
                  g_event_dev ? g_event_dev : "unknown");
    screen_printf("AP3216C path : %s\n",
                  ap_path[0] ? ap_path : "(not found)");
    screen_printf("ICM20608 path: %s\n",
                  icm_path[0] ? icm_path : "(not found)");
    screen_printf("\nTouch again to refresh...\n");
    screen_printf("========================================\n");
    fflush(stdout);
}

/*
 * 时间防抖触发：距上次触发不足 interval_ms 则跳过，
 * 避免 BTN_TOUCH release 丢失导致的状态卡死。
 */
static void trigger_once_if_allowed(const char *ap_path,
                                    const char *icm_path,
                                    long long *last_trigger_ms,
                                    long long interval_ms)
{
    long long now = get_time_ms();

    if (now - *last_trigger_ms < interval_ms)
        return;

    *last_trigger_ms = now;
    printf("[DEBUG] trigger sensor read\n");
    display_all_sensor_data(ap_path, icm_path);
}

/*
 * 在 tty 上显示初始等待界面
 */
static void show_idle_screen(const char *input_dev_path,
                             const char *ap_path,
                             const char *icm_path)
{
    if (!g_tty_fp)
        return;

    display_clear();
    screen_printf("========================================\n");
    screen_printf(" Touch Sensor Trigger Demo\n");
    screen_printf("========================================\n");
    screen_printf("Listening: %s\n", input_dev_path);
    screen_printf("Touch screen to read sensors...\n");
    screen_printf("\n");
    screen_printf("AP3216C : %s\n", ap_path[0] ? ap_path : "(not found)");
    screen_printf("ICM20608: %s\n", icm_path[0] ? icm_path : "(not found)");
    screen_printf("========================================\n");
    fflush(stdout);
}

/* ── 主程序 ── */

int main(int argc, char *argv[])
{
    const char *input_dev_path = NULL;
    const char *tty_path       = NULL;
    int i;

    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tty") == 0 && i + 1 < argc) {
            tty_path = argv[++i];
        } else if (argv[i][0] != '-') {
            input_dev_path = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!input_dev_path) {
        usage(argv[0]);
        return 1;
    }

    g_event_dev = input_dev_path;

    /* 打开 input 设备 */
    int fd = open(input_dev_path, O_RDONLY);
    if (fd < 0) {
        perror("open input device");
        return 1;
    }
    printf("Listening on %s ...\n", input_dev_path);

    /* 打开 TTY 设备 */
    if (tty_path) {
        g_tty_fp = display_open_tty(tty_path);
        if (g_tty_fp)
            printf("Screen output: %s\n", tty_path);
    } else {
        printf("[INFO] no --tty specified, output to stdout only\n");
    }

    /* 自动查找 IIO 设备 */
    char ap_path[MAX_PATH]  = {0};
    char icm_path[MAX_PATH] = {0};

    if (find_iio_device_by_name("ap3216c", ap_path, sizeof(ap_path)) == 0)
        printf("Found  AP3216C  at %s\n", ap_path);
    else
        printf("[INFO] AP3216C not found, will skip\n");

    if (find_iio_device_by_name("icm20608", icm_path, sizeof(icm_path)) == 0)
        printf("Found  ICM20608 at %s\n", icm_path);
    else
        printf("[INFO] ICM20608 not found, will skip\n");

    /* 屏幕显示初始等待界面 */
    show_idle_screen(input_dev_path, ap_path, icm_path);
    printf("Waiting for touch event... (Ctrl+C to exit)\n\n");

    /* 事件循环 */
    struct input_event ev;
    int           touching    = 0;
    long long     last_trigger_ms  = 0;
    const long long trigger_interval_ms = 800;  /* 800ms 防抖间隔 */

    while (1) {
        ssize_t n = read(fd, &ev, sizeof(ev));

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read input event");
            break;
        }
        if (n != sizeof(ev))
            continue;

        /* ── BTN_TOUCH ── */
        if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            printf("[DEBUG] BTN_TOUCH value=%d\n", ev.value);

            if (ev.value == 1) {
                touching = 1;
                trigger_once_if_allowed(ap_path, icm_path,
                                        &last_trigger_ms,
                                        trigger_interval_ms);
            } else if (ev.value == 0) {
                touching = 0;
            }
            continue;
        }

        /*
         * SYN_REPORT: 一帧结束，重置 touching。
         * 即使 BTN_TOUCH=0 没来，SYN_REPORT 也会协助解锁。
         */
        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (touching) {
                /* 连续多帧 SYN 之间保留 800ms 间隔 */
                trigger_once_if_allowed(ap_path, icm_path,
                                        &last_trigger_ms,
                                        trigger_interval_ms);
            }
            touching = 0;
            continue;
        }

#if TRIGGER_BY_ABS_IF_NO_BTN
        if (ev.type == EV_ABS &&
            (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y)) {
            trigger_once_if_allowed(ap_path, icm_path,
                                    &last_trigger_ms,
                                    trigger_interval_ms);
            touching = 1;
            continue;
        }
#endif
    }

    display_close();
    close(fd);
    return 0;
}
