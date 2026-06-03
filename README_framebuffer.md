# touch_sensor_fb — Framebuffer 直接绘制版多传感器联动 Demo

## 1. 项目目的

通过 GT911 input event 触发 AP3216C 和 ICM20608 的 IIO 数据读取，并**直接 mmap `/dev/fb0` 绘制界面到 LCD**，不依赖 Qt / SDL / TTY / 桌面环境。

用于学习 Linux **input、IIO、framebuffer** 三个内核接口的用户态联动。

## 2. 架构

```
GT911 driver              → /dev/input/eventX
AP3216C / ICM20608 driver → /sys/bus/iio/devices/iio:deviceX

touch_sensor_fb
  ├─ 监听 eventX (blocking read)
  ├─ 读取 IIO sysfs
  ├─ mmap /dev/fb0
  └─ 直接绘制像素 + 8x16 点阵文字
```

## 3. Framebuffer vs TTY vs Qt

| | TTY 版 | Framebuffer 版 | Qt |
|--|--------|----------------|----|
| 输出目标 | `/dev/tty1` (系统控制台) | `/dev/fb0` (显存) | 窗口系统 |
| 依赖 | 控制台驱动 | 仅 fb 驱动 | Qt 库 + 桌面 |
| 绘制方式 | ANSI escape + printf | mmap + 逐像素 | QPainter |
| 字体 | 控制台字体 | 自建 8x16 点阵 | 系统字体 |
| 适用场景 | 快速调试 | 无桌面环境的产品 | 复杂 UI |

## 4. 编译

```bash
# 宿主机测试
gcc -o touch_sensor_fb touch_sensor_fb.c fb_draw.c -Wall -O2

# ARM 交叉编译
arm-linux-gnueabihf-gcc -o touch_sensor_fb touch_sensor_fb.c fb_draw.c -Wall -O2

# 或用项目 Makefile
make app
```

如编译报 `clock_gettime` 未定义，加 `-lrt`：

```bash
arm-linux-gnueabihf-gcc -o touch_sensor_fb touch_sensor_fb.c fb_draw.c -lrt
```

## 5. 运行前检查

### 5.1 Framebuffer

```bash
ls /dev/fb*
cat /proc/fb
dmesg | grep -i fb
```

期望输出类似 `mxsfb 21c8000.lcdif: initialized`。

### 5.2 Input event

```bash
cat /proc/bus/input/devices
```

找到 `GT911 Touchscreen` 对应的 event 编号。

### 5.3 IIO 设备

```bash
for d in /sys/bus/iio/devices/iio:device*; do
    echo "===== $d ====="
    cat "$d/name"
    ls "$d" | grep raw
done
```

## 6. 运行

```bash
# 默认 /dev/fb0
./touch_sensor_fb /dev/input/event1

# 指定 framebuffer
./touch_sensor_fb /dev/input/event1 --fb /dev/fb0
```

触摸屏幕，LCD 上应当立即显示传感器数据。

## 7. 故障排查

| 现象 | 可能原因 | 排查 |
|------|---------|------|
| 屏幕黑屏 | fb0 未初始化或程序退出 | `dmesg \| grep -i fb` 确认 LCD 驱动加载 |
| 屏幕黑屏 | bpp 不支持 | 仅支持 16/32bpp，检查 `fbset -i` |
| 触摸只触发一次 | 时间防抖卡住 | 已使用 800ms 间隔，不依赖 release |
| 无传感器数据 | IIO 驱动未加载 | 先运行 TTY 版确认 IIO 正常 |
| 编译报 clock_gettime | 缺少 -lrt | 加 `-lrt` 链接 |
| fb_init 失败 | 权限不够 | 确认 `/dev/fb0` 可读写 |

## 8. 简历项目描述

> 设计并实现基于 Linux input、IIO 与 framebuffer 的多传感器触发显示 demo：通过 GT911 触摸屏 input event 触发，在用户态读取 AP3216C 光照/接近传感器与 ICM20608 六轴 IMU 的 IIO sysfs 数据，并直接 mmap `/dev/fb0` 绘制实时数据界面，自建 8×16 点阵字体渲染引擎，实现嵌入式 Linux 多子系统接口（input / IIO / framebuffer）的联动验证。
