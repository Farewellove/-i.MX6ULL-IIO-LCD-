/**
 * touch_sensor_fb.c — Framebuffer 多传感器联动 Demo
 *
 * 阶段隔离测试模式：
 *   --fb-init-only-input-test    fb_init 后直接 event loop
 *   --fb-clear-only-input-test   fb_init + fb_clear 后 event loop
 *   --fb-draw-wait-input-test    fb_init + draw_wait_screen 后 event loop (等同正常模式)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include "fb_draw.h"

#define IIO_BASE  "/sys/bus/iio/devices"
#define MAX_PATH  512

static volatile sig_atomic_t g_stop = 0;
static int g_debug_input = 0;
static int g_no_fb       = 0;
static int g_fb_test     = 0;
static int g_abs_trigger = 0;
static int g_draw_once   = 0;
static int g_force_loop  = 0;
static int g_input_only  = 0;
static int g_fb_init_only_test = 0;
static int g_fb_clear_only_test = 0;
static int g_draw_wait   = 0;   /* --draw-wait */
static int g_reset_after_draw = 0; /* --reset-after-draw */
static int g_reset_delay_ms    = 1200; /* --reset-delay-ms */
static int g_reset_gt911_once  = 0; /* --reset-gt911 */
static const char *g_force_reset_path = "/sys/bus/i2c/devices/1-005d/force_reset";

static void gt911_force_reset_after_draw(void)
{
    FILE *fp;
    if (!g_reset_after_draw) {
        printf("[INFO] skip GT911 force_reset after draw\n");
        return;
    }
    printf("[INFO] wait %d ms before GT911 force_reset\n", g_reset_delay_ms);
    usleep(g_reset_delay_ms * 1000);
    fp = fopen(g_force_reset_path, "w");
    if (!fp) { printf("[WARN] cannot open %s\n", g_force_reset_path); return; }
    fprintf(fp, "1\n"); fclose(fp);
    printf("[INFO] GT911 force_reset done\n");
}

/* 独立 reset-gt911 模式：写 force_reset 后退出 */
static int do_gt911_reset_once(void)
{
    FILE *fp = fopen(g_force_reset_path, "w");
    if (!fp) { fprintf(stderr, "cannot open %s\n", g_force_reset_path); return 1; }
    fprintf(fp, "1\n"); fclose(fp);
    printf("[INFO] GT911 force_reset written\n");
    return 0;
}
static int g_fb_draw_wait_test  = 0;

static void handle_signal(int sig) { (void)sig; g_stop = 1; }

/* ── 工具 ── */
static long long get_time_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int is_name_match(const char *a, const char *b) {
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a == '\0' && *b == '\0';
}
static int read_sysfs_int(const char *path, int *val) {
    FILE *fp = fopen(path, "r"); if (!fp) return -1;
    char buf[32] = {0}; if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    fclose(fp); *val = atoi(buf); return 0;
}
static int find_iio_device(const char *target, char *out, size_t sz) {
    DIR *d = opendir(IIO_BASE); if (!d) { perror(IIO_BASE); return -1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "iio:device", 10)) continue;
        char np[MAX_PATH]; snprintf(np, sizeof(np), IIO_BASE "/%s/name", e->d_name);
        FILE *fp = fopen(np, "r"); if (!fp) continue;
        char name[64] = {0}; fgets(name, sizeof(name), fp); fclose(fp);
        name[strcspn(name, "\r\n")] = 0;
        if (is_name_match(name, target)) {
            snprintf(out, sz, IIO_BASE "/%s/", e->d_name);
            closedir(d); return 0;
        }
    }
    closedir(d); return -1;
}

/* 读 /proc/interrupts 中 gt911 行 */
static void print_gt911_irq(void) {
    FILE *fp = fopen("/proc/interrupts", "r"); if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "gt911")) {
            /* 去尾部换行 */
            line[strcspn(line, "\r\n")] = 0;
            printf("[DEBUG] /proc/interrupts gt911: %s\n", line);
            break;
        }
    }
    fclose(fp);
}

/* ── 全局数据 ── */
static char g_ap[MAX_PATH]  = {0};
static char g_icm[MAX_PATH] = {0};
static const char *g_event  = NULL;
static long long g_last_trigger = 0;
static const long long TRIGGER_INTERVAL = 800;

/* ── 传感器 ── */
struct sensor_data {
    int ap_als, ap_ir, ap_ps, ap_ok;
    int icm_ax, icm_ay, icm_az, icm_gx, icm_gy, icm_gz, icm_temp, icm_ok;
};
static void read_sensor_values(struct sensor_data *sd) {
    char p[MAX_PATH]; memset(sd, 0, sizeof(*sd));
    if (g_ap[0]) {
        snprintf(p, sizeof(p), "%s/in_illuminance_raw", g_ap); sd->ap_ok  = !read_sysfs_int(p, &sd->ap_als);
        snprintf(p, sizeof(p), "%s/in_intensity_ir_raw", g_ap); sd->ap_ok &= !read_sysfs_int(p, &sd->ap_ir);
        snprintf(p, sizeof(p), "%s/in_proximity_raw", g_ap);    sd->ap_ok &= !read_sysfs_int(p, &sd->ap_ps);
    }
    if (g_icm[0]) {
        snprintf(p, sizeof(p), "%s/in_accel0_raw",   g_icm); sd->icm_ok  = !read_sysfs_int(p, &sd->icm_ax);
        snprintf(p, sizeof(p), "%s/in_accel1_raw",   g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_ay);
        snprintf(p, sizeof(p), "%s/in_accel2_raw",   g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_az);
        snprintf(p, sizeof(p), "%s/in_anglvel0_raw", g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_gx);
        snprintf(p, sizeof(p), "%s/in_anglvel1_raw", g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_gy);
        snprintf(p, sizeof(p), "%s/in_anglvel2_raw", g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_gz);
        snprintf(p, sizeof(p), "%s/in_temp0_raw",    g_icm); sd->icm_ok &= !read_sysfs_int(p, &sd->icm_temp);
    }
}
static void print_sensor_values(const struct sensor_data *sd) {
    printf("=== SENSOR DATA ===\n");
    if (sd->ap_ok)  printf("AP3216C ALS=%d IR=%d PS=%d\n", sd->ap_als, sd->ap_ir, sd->ap_ps);
    else            printf("AP3216C n/a\n");
    if (sd->icm_ok) printf("ICM20608 Accel=(%d,%d,%d) Gyro=(%d,%d,%d) Temp=%d\n",
                           sd->icm_ax, sd->icm_ay, sd->icm_az,
                           sd->icm_gx, sd->icm_gy, sd->icm_gz, sd->icm_temp);
    else            printf("ICM20608 n/a\n");
    printf("===================\n");
}

/* ── UI ── */
#define LX 20
#define LH 22
static void draw_header(void) {
    fb_fill_rect(0, 0, fb_width(), 42, FB_DARKBLUE);
    fb_draw_string(LX, 12, "Touch-triggered Sensor Acquisition (FB)", FB_WHITE, FB_DARKBLUE);
}
static void draw_sensor_screen(const struct sensor_data *sd) {
    fb_clear(FB_BLACK); draw_header(); int y = 60;
    fb_draw_string(LX, y, "[GT911]", FB_WHITE, FB_BLACK); y += LH;
    fb_draw_printf(LX, y, FB_GRAY, FB_BLACK, "  Event: %s", g_event ? g_event : "?"); y += LH;
    fb_draw_printf(LX, y, FB_GREEN, FB_BLACK, "  Status: Touch detected"); y += LH + 4;
    fb_draw_string(LX, y, "[AP3216C]", FB_GREEN, FB_BLACK); y += LH;
    if (g_ap[0] && sd->ap_ok) {
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  ALS=%d  -> %d lux", sd->ap_als, sd->ap_als); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  IR=%d", sd->ap_ir); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  PS=%d", sd->ap_ps); y += LH;
    } else { fb_draw_printf(LX, y, FB_YELLOW, FB_BLACK, "  (not found)"); y += LH; }
    y += 4; fb_draw_string(LX, y, "[ICM20608]", FB_CYAN, FB_BLACK); y += LH;
    if (g_icm[0] && sd->icm_ok) {
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Accel X=%6d  %+.2f g",   sd->icm_ax, sd->icm_ax / 2048.0f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Accel Y=%6d  %+.2f g",   sd->icm_ay, sd->icm_ay / 2048.0f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Accel Z=%6d  %+.2f g",   sd->icm_az, sd->icm_az / 2048.0f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Gyro  X=%6d  %+.2f d/s", sd->icm_gx, sd->icm_gx / 16.4f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Gyro  Y=%6d  %+.2f d/s", sd->icm_gy, sd->icm_gy / 16.4f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Gyro  Z=%6d  %+.2f d/s", sd->icm_gz, sd->icm_gz / 16.4f); y += LH;
        fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "  Temp    =%6d  %.2f C",    sd->icm_temp, sd->icm_temp / 326.8f + 25.0f); y += LH;
    } else { fb_draw_printf(LX, y, FB_YELLOW, FB_BLACK, "  (not found)"); y += LH; }
    y += 8; fb_draw_string(LX, y, "Touch again / Ctrl+C to exit", FB_YELLOW, FB_BLACK);
}
static void draw_wait_screen(void) {
    fb_clear(FB_BLACK); draw_header(); int y = 60;
    fb_draw_string(LX, y, "Touch Sensor Framebuffer Demo", FB_WHITE, FB_BLACK); y += LH + 4;
    fb_draw_printf(LX, y, FB_WHITE, FB_BLACK, "Listening: %s", g_event ? g_event : "?"); y += LH;
    fb_draw_printf(LX, y, FB_GRAY, FB_BLACK, "Framebuffer: %dx%d %dbpp", fb_width(), fb_height(), fb_bpp()); y += LH;
    fb_draw_printf(LX, y, g_ap[0] ? FB_GREEN : FB_YELLOW, FB_BLACK,
                   "AP3216C : %s", g_ap[0] ? g_ap : "(not found)"); y += LH;
    fb_draw_printf(LX, y, g_icm[0] ? FB_CYAN : FB_YELLOW, FB_BLACK,
                   "ICM20608: %s", g_icm[0] ? g_icm : "(not found)"); y += LH + 10;
    fb_draw_string(LX, y, "Touch screen / Ctrl+C to exit", FB_YELLOW, FB_BLACK);
}
static void fb_self_test(void) {
    fb_clear(FB_BLACK);
    fb_fill_rect(20, 20, 200, 40, FB_RED);    fb_draw_string(240, 30,  "RED bar",   FB_WHITE, FB_BLACK);
    fb_fill_rect(20, 70, 200, 40, FB_GREEN);  fb_draw_string(240, 80,  "GREEN bar", FB_WHITE, FB_BLACK);
    fb_fill_rect(20, 120, 200, 40, FB_BLUE);  fb_draw_string(240, 130, "BLUE bar",  FB_WHITE, FB_BLACK);
    fb_fill_rect(20, 170, 200, 40, FB_WHITE); fb_draw_string(240, 180, "WHITE bar", FB_WHITE, FB_BLACK);
    fb_draw_string(20, 240, "Framebuffer OK  Font OK  0123456789", FB_GREEN, FB_BLACK);
    fb_draw_printf(20, 290, FB_WHITE, FB_BLACK, "fb: %dx%d %dbpp", fb_width(), fb_height(), fb_bpp());
    printf("[INFO] fb-test done\n"); sleep(5);
}

/* ── 唯一触发入口 ── */
static void try_trigger_sensor_read(void) {
    long long now = get_time_ms();
    if (now - g_last_trigger < TRIGGER_INTERVAL) {
        printf("[DEBUG] trigger ignored by debounce\n"); return;
    }
    g_last_trigger = now;
    printf("[DEBUG] trigger sensor read\n");

    struct sensor_data sd; read_sensor_values(&sd); print_sensor_values(&sd);

    if (g_input_only) {
        printf("[DEBUG] input-only mode, skip sensor draw\n"); return;
    }
    if (g_no_fb) {
        printf("[DEBUG] no-fb mode, skip framebuffer draw\n"); return;
    }

    printf("[DEBUG] draw_sensor_screen enter\n");
    draw_sensor_screen(&sd);
    printf("[DEBUG] draw_sensor_screen done\n");
    usleep(100000); /* 等 LCD 刷新稳定 */
    gt911_force_reset_after_draw();
}

/* ── 事件处理 ── */
static const char *ev_label(unsigned t, unsigned c) {
    if (t == EV_SYN && c == SYN_REPORT)        return "SYN_REPORT";
    if (t == EV_KEY && c == BTN_TOUCH)         return "BTN_TOUCH";
    if (t == EV_ABS && c == ABS_MT_POSITION_X) return "ABS_MT_POSITION_X";
    if (t == EV_ABS && c == ABS_MT_POSITION_Y) return "ABS_MT_POSITION_Y";
    if (t == EV_ABS && c == ABS_X)             return "ABS_X";
    if (t == EV_ABS && c == ABS_Y)             return "ABS_Y";
    return "";
}
static void process_input_event(const struct input_event *ev) {
    const char *nm = ev_label(ev->type, ev->code);
    if (g_debug_input) {
        if (nm[0]) printf("[EV] type=%u code=%u val=%d (%s)\n", ev->type, ev->code, ev->value, nm);
        else       printf("[EV] type=%u code=%u val=%d\n", ev->type, ev->code, ev->value);
    }
    if (g_input_only) return;
    if (ev->type == EV_KEY && ev->code == BTN_TOUCH && ev->value == 1)
        { printf("[DEBUG] BTN_TOUCH=1, trigger\n"); try_trigger_sensor_read(); return; }
    if (g_abs_trigger && ev->type == EV_ABS &&
        (ev->code == ABS_X || ev->code == ABS_Y ||
         ev->code == ABS_MT_POSITION_X || ev->code == ABS_MT_POSITION_Y))
        { printf("[DEBUG] ABS coord, trigger\n"); try_trigger_sensor_read(); return; }
}

/* ══════ 统一事件循环 ══════ */
static int run_input_event_loop(int fd) {
    struct pollfd pfd; pfd.fd = fd; pfd.events = POLLIN;
    struct input_event ev;
    long long last_hb = 0, last_irq = 0, last_force = 0;

    printf("[INFO] entering shared input event loop\n");
    while (!g_stop) {
        int ret = poll(&pfd, 1, 200);
        if (ret < 0) { if (errno == EINTR) continue; perror("poll"); return -1; }
        if (ret == 0) {
            long long now = get_time_ms();
            if (g_force_loop && now - last_force >= 2000) { last_force = now; try_trigger_sensor_read(); }
            if (now - last_hb > 5000) {
                last_hb = now;
                printf("[DEBUG] waiting input event...\n");
            }
            if (now - last_irq > 3000) {
                last_irq = now;
                print_gt911_irq();
            }
            continue;
        }
        if (pfd.revents & POLLERR)  printf("[WARN] POLLERR\n");
        if (pfd.revents & POLLHUP)  printf("[WARN] POLLHUP\n");
        if (pfd.revents & POLLNVAL) { printf("[WARN] POLLNVAL\n"); return -1; }
        if (!(pfd.revents & POLLIN)) continue;

        while (1) {
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) break; if (errno == EINTR) continue; perror("read"); return -1; }
            if (n != sizeof(ev)) break;
            process_input_event(&ev);
        }
    }
    printf("[INFO] event loop exited\n"); return 0;
}

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s /dev/input/eventX [OPTIONS]\n\n"
        "Options:\n"
        "  --fb /dev/fb0              framebuffer (default /dev/fb0)\n"
        "  --no-fb                    stdout only\n"
        "  --input-only               only listen input, no IIO/fb\n"
        "  --debug-input              print every input_event\n"
        "  --fb-test                  fb self-test then exit\n"
        "  --draw-once                draw sensor screen once then exit\n"
        "  --force-trigger-loop       auto-trigger every 2s\n"
        "  --abs-trigger              also trigger on ABS_X/Y/MT\n"
        "  --fb-init-only-input-test  fb_init only, then event loop\n"
        "  --fb-clear-only-input-test fb_init+clear, then event loop\n"
        "  --fb-draw-wait-input-test  fb_init+draw_wait, then event loop\n"
        "  --draw-wait              draw wait screen at startup (default: skip)\n"
        "  --no-reset-after-draw    skip GT911 force_reset after framebuffer draw\n\n"
        "Examples:\n"
        "  %s /dev/input/event1 --input-only --debug-input\n"
        "  %s /dev/input/event1 --no-fb --debug-input\n"
        "  %s /dev/input/event1 --fb-init-only-input-test --debug-input\n",
        p, p, p, p);
}

int main(int argc, char *argv[]) {
    const char *event_dev = NULL, *fb_dev = "/dev/fb0";
    int i;

    for (i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--fb") && i + 1 < argc) fb_dev = argv[++i];
        else if (!strcmp(argv[i], "--debug-input"))              g_debug_input = 1;
        else if (!strcmp(argv[i], "--no-fb"))                    g_no_fb = 1;
        else if (!strcmp(argv[i], "--input-only"))               g_input_only = 1;
        else if (!strcmp(argv[i], "--fb-test"))                  g_fb_test = 1;
        else if (!strcmp(argv[i], "--draw-once"))                g_draw_once = 1;
        else if (!strcmp(argv[i], "--force-trigger-loop"))       g_force_loop = 1;
        else if (!strcmp(argv[i], "--abs-trigger"))              g_abs_trigger = 1;
        else if (!strcmp(argv[i], "--fb-init-only-input-test"))  g_fb_init_only_test = 1;
        else if (!strcmp(argv[i], "--fb-clear-only-input-test")) g_fb_clear_only_test = 1;
        else if (!strcmp(argv[i], "--fb-draw-wait-input-test"))  g_fb_draw_wait_test = 1;
        else if (!strcmp(argv[i], "--draw-wait"))                g_draw_wait = 1;
        else if (!strcmp(argv[i], "--reset-after-draw"))         g_reset_after_draw = 1;
        else if (!strcmp(argv[i], "--reset-delay-ms") && i+1 < argc)
            g_reset_delay_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reset-gt911"))              g_reset_gt911_once = 1;
        else if (argv[i][0] != '-')                              event_dev = argv[i];
        else { usage(argv[0]); return 1; }
    }
    if (!event_dev) { usage(argv[0]); return 1; }
    g_event = event_dev;

    /* --reset-gt911: standalone, 只写 force_reset 后退出 */
    if (g_reset_gt911_once) {
        int rc = do_gt911_reset_once();
        return rc;
    }

    /* sigaction */
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal; sigemptyset(&sa.sa_mask); sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    printf("[INFO] input path: %s\n", event_dev);
    printf("[INFO] fb path: %s\n", fb_dev);
    printf("[INFO] mode: input_only=%d no_fb=%d debug=%d abs=%d force=%d "
           "fb_init_test=%d fb_clear_test=%d fb_draw_test=%d\n",
           g_input_only, g_no_fb, g_debug_input, g_abs_trigger, g_force_loop,
           g_fb_init_only_test, g_fb_clear_only_test, g_fb_draw_wait_test);

    /* open input fd */
    int g_input_fd = open(event_dev, O_RDONLY | O_NONBLOCK);
    if (g_input_fd < 0) { perror("open input"); return 1; }
    char name[256] = {0};
    if (ioctl(g_input_fd, EVIOCGNAME(sizeof(name)), name) >= 0)
        printf("[INFO] input device: %s\n", name);
    printf("[INFO] input_fd=%d\n", g_input_fd);

    /* input-only */
    if (g_input_only) {
        printf("[INFO] input-only mode\n");
        run_input_event_loop(g_input_fd);
        close(g_input_fd);
        return 0;
    }

    /* IIO */
    if (find_iio_device("ap3216c", g_ap, sizeof(g_ap)) == 0)
        printf("[INFO] AP3216C found: %s\n", g_ap);
    else printf("[INFO] AP3216C not found\n");
    if (find_iio_device("icm20608", g_icm, sizeof(g_icm)) == 0)
        printf("[INFO] ICM20608 found: %s\n", g_icm);
    else printf("[INFO] ICM20608 not found\n");

    /* no-fb */
    if (g_no_fb) {
        printf("[INFO] no-fb mode\n");
        run_input_event_loop(g_input_fd);
        close(g_input_fd);
        return 0;
    }

    /* ── fb mode ── */
    printf("[INFO] normal fb mode, init framebuffer...\n");
    if (fb_init(fb_dev) < 0) { close(g_input_fd); return 1; }
    printf("[INFO] framebuffer initialized, fb_fd ok\n");

    /* fb-test */
    if (g_fb_test) {
        fb_self_test(); fb_close(); close(g_input_fd); return 0;
    }
    /* draw-once */
    if (g_draw_once) {
        struct sensor_data sd; read_sensor_values(&sd); print_sensor_values(&sd);
        draw_sensor_screen(&sd); sleep(5);
        fb_clear(FB_BLACK); fb_close(); close(g_input_fd); return 0;
    }

    /* ── 阶段隔离测试 ── */
    if (g_fb_init_only_test) {
        printf("[INFO] --fb-init-only-input-test: fb_init done, no draw, entering loop\n");
        run_input_event_loop(g_input_fd);
        fb_close(); close(g_input_fd); return 0;
    }
    if (g_fb_clear_only_test) {
        printf("[INFO] --fb-clear-only-input-test: fb_init+clear, no draw text, entering loop\n");
        fb_clear(FB_BLACK);
        run_input_event_loop(g_input_fd);
        fb_close(); close(g_input_fd); return 0;
    }
    if (g_fb_draw_wait_test || g_draw_wait) {
        printf("[INFO] --draw-wait: drawing wait screen, entering loop\n");
        draw_wait_screen();
    } else {
        printf("[INFO] normal fb mode, skip wait screen to avoid GT911 interference\n");
        printf("[INFO] touch screen to draw sensor screen\n");
    }

    run_input_event_loop(g_input_fd);

    fb_clear(FB_BLACK);
    fb_draw_string(LX, 20, "touch_sensor_fb exited.", FB_WHITE, FB_BLACK);
    fb_close(); close(g_input_fd);
    printf("[INFO] exited\n");
    return 0;
}
