# touch_sensor_trigger — Linux 多传感器联动综合学习 Demo

## 1. 项目目的

通过 GT911 触摸屏 input event 触发 AP3216C 和 ICM20608 的 IIO 数据读取，用于学习：

- Linux **input 子系统**：`/dev/input/eventX` 事件监听
- Linux **IIO 子系统**：`/sys/bus/iio/devices/` sysfs 读取
- **用户态事件驱动联动**：不同内核子系统在用户空间解耦协作

## 2. 架构说明

```
┌──────────────────────────────┐
│         GT911 driver         │  input 子系统
│    → /dev/input/eventX       │
└──────────────┬───────────────┘
               │ ev.type=EV_KEY, code=BTN_TOUCH, value=1
               ▼
┌──────────────┴───────────────┐
│   touch_sensor_trigger       │  用户态程序
│   - 阻塞监听 input event     │
│   - 触摸按下 → 读 IIO sysfs  │
│   - 打印传感器原始值          │
└──────┬────────────────┬──────┘
       │                │
       ▼                ▼
┌──────────────┐ ┌──────────────┐
│ AP3216C drv  │ │ ICM20608 drv │  IIO 子系统
│ /sys/bus/iio │ │ /sys/bus/iio │
│  /devices/   │ │  /devices/   │
│  iio:deviceX │ │  iio:deviceY │
└──────────────┘ └──────────────┘
```

## 3. 为什么不在内核里让 GT911 直接调用 AP3216C / ICM20608？

| 方面 | 内核耦合 | 用户态联动 |
|------|---------|-----------|
| 子系统隔离 | 破坏：input 驱动直接依赖 IIO | 保持：各自独立注册 |
| 错误隔离 | 传感器读失败可能影响触摸上报 | 传感器失败只打印 warning，不影响触摸 |
| 调试难度 | 需要 dmesg，崩溃影响全局 | printf + gdb，崩溃只影响本进程 |
| 可维护性 | 修改联动逻辑需重新编译内核模块 | 改用户代码，秒级重新编译 |
| 驱动复用 | 逻辑写死在某个驱动里，换芯片要改驱动 | 一个程序适配所有兼容 input/IIO 的设备 |

**结论**：不同内核子系统之间的业务联动，放在用户态更清晰、更安全、更易调试。

## 4. 编译方法

### 宿主机编译

```bash
make -f Makefile_touch_sensor_trigger
```

### 交叉编译（ARM 目标板）

```bash
make -f Makefile_touch_sensor_trigger CC=arm-linux-gnueabihf-gcc
```

或直接：

```bash
arm-linux-gnueabihf-gcc -Wall -Wextra -O2 -o touch_sensor_trigger touch_sensor_trigger.c
```

## 5. 运行前检查

### 5.1 确认 GT911 event 节点

```bash
cat /proc/bus/input/devices
```

找到：

```
N: Name="GT911 Touchscreen"
H: Handlers=mouse0 event1
```

则 GT911 对应 `/dev/input/event1`。

### 5.2 确认 IIO 设备存在

```bash
ls /sys/bus/iio/devices/

for d in /sys/bus/iio/devices/iio:device*; do
    echo "===== $d ====="
    cat "$d/name"
    ls "$d" | grep raw
done
```

期望看到：

```
===== /sys/bus/iio/devices/iio:device0 =====
ap3216c
in_illuminance_raw
in_intensity_ir_raw
in_proximity_raw

===== /sys/bus/iio/devices/iio:device1 =====
icm20608
in_accel0_raw
in_accel1_raw
...
```

### 5.3 确认触摸有数据

```bash
hexdump /dev/input/event1
# 触摸屏幕，看是否有数据刷新
```

## 6. 显示到 LCD/HDMI 屏幕

### 6.1 SSH vs 本地屏幕

通过 SSH 运行程序时，`printf` 只会显示在 SSH 终端，**不会**显示在开发板连接的物理屏幕上。

要将数据输出到开发板的 LCD/HDMI 屏幕，需要使用 `--tty` 参数。

### 6.2 启用 TTY 屏幕显示

```bash
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty1
```

程序会：
- 清空屏幕
- 将传感器数据同时输出到 SSH 终端和物理屏幕
- 每次触摸刷新屏幕内容

### 6.3 如果屏幕不显示

```bash
# 确认当前活动的 tty
who
ls /dev/tty*

# 确认 framebuffer 是否正常
cat /proc/fb
dmesg | grep -i fb

# 尝试其他 tty
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty0
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty1
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty2
```

如果 `/dev/tty1` 无效果，可能需要 sudo 权限。`/dev/tty*` 写入需要 root 权限。

如果系统运行的是桌面环境（X11/Wayland），tty 输出不会显示在图形桌面窗口中，需要在本地终端运行。

### 6.4 预留方案 B：framebuffer 直接绘制

后续如需不依赖 tty，可扩展 `--fb /dev/fb0` 模式：mmap framebuffer 并绘制文字。本次优先完成 `--tty` 版本。

---

## 7. 运行

```bash
# 只在 SSH 终端显示
./touch_sensor_trigger /dev/input/event1

# 同时显示到 SSH 终端 + LCD 屏幕
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty1
```

触摸屏幕一次，预期输出：

```
Found  AP3216C  at /sys/bus/iio/devices/iio:device0/
Found  ICM20608 at /sys/bus/iio/devices/iio:device1/
Waiting for touch event...

=====================================
Touch detected, read sensors once
=====================================
[AP3216C]
  ALS raw = 245
  IR  raw = 32
  PS  raw = 0

[ICM20608]
  Accel X raw = 204
  Accel Y raw = -56
  Accel Z raw = 1980
  Gyro  X raw = 15
  Gyro  Y raw = -8
  Gyro  Z raw = 3
  Temp raw    = 1234

=====================================
```

## 8. 故障排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 找不到 AP3216C / ICM20608 | 驱动未加载 | `lsmod` 确认 ap3216c、icm20608 已加载 |
| 找不到 IIO 节点 | IIO device 未注册 | 检查 `dmesg \| grep -E "ap3216c\|icm20608"` |
| sysfs 节点名不同 | 内核版本差异导致节点名变化 | `ls /sys/bus/iio/devices/iio:deviceX/ \| grep raw` 确认实际名称 |
| 触摸不触发 | eventX 选错 | `cat /proc/bus/input/devices` 确认 GT911 对应 event 编号 |
| 触摸不触发 | GT911 无 BTN_TOUCH | 程序已兼容 ABS_MT 事件（`TRIGGER_BY_ABS_IF_NO_BTN=1`） |
| 触摸不触发 | GT911 模块未加载 | `dmesg \| grep GT911` 确认 probe 成功 |
| 第一次有效，后续无反应 | BTN_TOUCH=0 未稳定上报导致 touching 卡死 | 已改为**时间防抖**（800ms 间隔），不依赖 release 事件 |
| 屏幕全黑不刷新 | display_clear() 后无内容输出 | 已增加 `show_idle_screen()` 启动即显示等待界面 |
| 重复触发 | 长按导致连续事件 | 800ms 时间间隔防抖 + SYN_REPORT 解锁 |

## 9. 简历项目描述

> 设计并实现基于 Linux input 与 IIO 子系统的多传感器联动测试程序：通过 GT911 触摸屏 input event 触发，用户态阻塞监听 `/dev/input/eventX`，在触摸按下瞬间读取 AP3216C 光照/接近传感器与 ICM20608 六轴 IMU 的 IIO sysfs 数据，实现不同内核子系统间的事件驱动式数据采集验证。涵盖设备树配置、input 事件解析、IIO sysfs 接口遍历、交叉编译与嵌入式部署。
