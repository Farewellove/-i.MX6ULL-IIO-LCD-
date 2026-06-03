# i.MX6ULL Linux 多传感器驱动综合项目

正点原子 i.MX6ULL ALPHA V2.4 + 7 寸 1024x600 LCD + GT911 触摸屏。

## 项目文件结构

```
.
├── kernel_drivers/               # 内核驱动
│   ├── ap3216c_iio/
│   │   ├── ap3216c.c             # AP3216C 光传感器 IIO 驱动 (I2C1, 0x1E)
│   │   └── ap3216creg.h          # 寄存器宏
│   ├── icm20608_iio/
│   │   ├── icm20608.c            # ICM20608 六轴 IMU IIO 驱动 (SPI3, CS0)
│   │   └── icm20608reg.h         # 寄存器宏
│   ├── gt911_ts/
│   │   └── lcd.c                 # GT911 电容触摸 input 驱动 (I2C2, 0x5D)
│   └── sensor_keys/
│       └── sensor_driver.c       # LED+按键 字符设备驱动 (platform)
│
├── user_app/                     # 用户态测试程序
│   ├── touch_sensor_trigger.c    # ★ TTY 版多传感器联动 (推荐)
│   ├── touch_sensor_fb.c         # Framebuffer 版多传感器联动 (学习参考)
│   ├── fb_draw.c / fb_draw.h     # Framebuffer 绘图模块 (16/32bpp + 点阵字体)
│   ├── font8x16.h                # ASCII 8x16 点阵字库
│   ├── read_all_sensor.c         # 全传感器采集 (ICM20608 + AP3216C + LED/KEY)
│   ├── input_test.c              # input_event 原始打印
│   ├── lcd_test.c                # LCD 纯色填充测试 (framebuffer)
│   ├── ap3216cAPP.c              # AP3216C IIO sysfs 读取
│   └── icm20608APP.c             # ICM20608 IIO sysfs 读取 + 单位转换
│
├── notes/
│   ├── GT911_driver_bringup_summary.md   # GT911 驱动移植经验总结
│   └── LCD_1024x600_timing.md           # 7 寸 LCD 时序配置笔记
│
├── load_drivers.sh               # 一键加载三个驱动模块
├── Makefile                      # 工程 Makefile
├── README.md                     # 本文档
├── README_touch_sensor_trigger.md # TTY 版详细文档
└── README_framebuffer.md          # Framebuffer 版详细文档
```

## 硬件连接

| 设备 | 总线 | 引脚/地址 |
|------|------|----------|
| AP3216C | I2C1 | 0x1E |
| ICM20608 | SPI3 (ecspi3) | CS0, 8MHz, MODE0 |
| GT911 | I2C2 | 0x5D, INT=GPIO1_IO09, RST=GPIO5_IO09 |
| LED | GPIO1_IO03 | `/dev/sensor_collect` |
| KEY0 | GPIO1_IO18 | `/dev/sensor_collect` |
| LCD (7寸 1024x600) | RGB LCDIF | 24bit, 51.2MHz pixel clock |

## 快速开始

```bash
# 1. 编译全部
make
make app
make install

# 2. 板端加载驱动
cd /lib/modules/4.1.15
./load_drivers.sh

# 3. ★ 运行 TTY 版传感器联动 (推荐)
./touch_sensor_trigger /dev/input/event1

# 4. ★ 显示到 LCD 屏幕 
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty1

# 5. Framebuffer 版 (学习/实验)
./touch_sensor_fb /dev/input/event1
```

## 四个驱动简介

### 1. AP3216C — 光传感器 (IIO)

```bash
insmod ap3216c.ko
# 读取数据
cat /sys/bus/iio/devices/iio:device0/in_illuminance_raw
```

| 通道 | 含义 | sysfs |
|------|------|-------|
| ALS | 环境光强度 | `in_illuminance_raw` → lux |
| PS  | 接近检测 | `in_proximity_raw` |
| IR  | 红外强度 | `in_intensity_ir_raw` |

### 2. ICM20608 — 六轴 IMU (IIO)

```bash
insmod icm20608.ko
# 读取数据
cat /sys/bus/iio/devices/iio:device1/in_accel0_raw
```

| 通道 | 含义 | sysfs | 换算公式 |
|------|------|-------|---------|
| Accel X/Y/Z | 加速度 | `in_accelX_raw` | g = raw / 2048 |
| Gyro X/Y/Z | 角速度 | `in_anglvelX_raw` | °/s = raw / 16.4 |
| Temp | 温度 | `in_temp0_raw` | °C = raw/326.8 + 25 |

### 3. GT911 — 电容触摸屏 (input)

```bash
insmod gt911_ts.ko
# 查看设备
cat /proc/bus/input/devices | grep GT911
# 测试触摸
./input_test /dev/input/event1
```

- 支持 5 点触摸
- BTN_TOUCH + ABS_MT_POSITION_X/Y 事件
- IRQ 需要每次清 0x814E，否则 INT 卡住
- `echo 1 > /sys/bus/i2c/devices/1-005d/force_reset` 可手动恢复

### 4. Sensor Keys — LED + 按键 (字符设备)

```bash
insmod sensor_driver.ko
# 控制 LED
echo 1 > /dev/sensor_collect   # 开灯
echo 0 > /dev/sensor_collect   # 关灯
# 按 KEY0 翻转 LED 状态（中断 + 消抖）
```

## ★ 推荐方案：TTY 版 touch_sensor_trigger

**这是最稳定、最简单的方案。**

原理：GT911 触摸 → 读 IIO sysfs → 写数据到 `/dev/tty1`（由内核 fbcon 渲染到 LCD）

```bash
# 仅在 SSH 终端显示
./touch_sensor_trigger /dev/input/event1

# 同时显示到 LCD 屏幕
sudo ./touch_sensor_trigger /dev/input/event1 --tty /dev/tty1
```

优势：
- 零硬件干扰：内核 fbcon 管理 framebuffer，用户态只写文本
- 稳定可靠：首次实现即跑通，无 I2C 冲突
- 代码简洁：单文件 ~300 行

## 学习参考：Framebuffer 版 touch_sensor_fb

**这是深入学习 Linux framebuffer、I2C 时序、中断处理的实验项目。**

原理：GT911 触摸 → 读 IIO sysfs → 自建 mmap + 点阵字体 → 直接绘制到 `/dev/fb0`

```bash
./touch_sensor_fb /dev/input/event1                 # 正常模式
./touch_sensor_fb /dev/input/event1 --fb-test        # framebuffer 自检
./touch_sensor_fb /dev/input/event1 --draw-once      # 传感器界面自检
./touch_sensor_fb /dev/input/event1 --force-trigger-loop  # 强制刷新测试
./touch_sensor_fb /dev/input/event1 -reset-gt911     # GT911 手动恢复
```

学习收获：
- mmap framebuffer 直接操作显存
- 16bpp RGB565 / 32bpp XRGB 像素格式转换
- 8x16 点阵字体渲染
- I2C 总线干扰的诊断（LCD 控制器刷显存时 I2C 短暂异常）
- GT911 IRQ handler 鲁棒性（读重试、限频日志、workqueue 恢复）
- 用户态事件驱动 + 时间防抖

## 开发环境

| 项目 | 版本 |
|------|------|
| 开发板 | 正点原子 i.MX6ULL ALPHA V2.4 |
| LCD | 7 寸 1024x600 (ATK-MD0700R-1024600) |
| 内核 | Linux 4.1.15 (出厂源码) |
| 交叉编译器 | arm-linux-gnueabihf-gcc |
| NFS 路径 | `/home/why/zdyz/nfs/rootfs/` |
| TFTP 路径 | `/home/why/zdyz/tftpboot/` |
| 源码路径 | `/home/why/zdyz/alientek_linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek` |

## 编译命令

```bash
make          # 编译所有内核模块
make app      # 编译所有用户态程序
make install  # 编译 + 部署到 NFS
make clean    # 清理
```

## 关键经验教训

1. **GPIO SNVS 域**：GPIO5_IO09 的 pinctrl 必须在 `&iomuxc_snvs` 下，不能放在 `&iomuxc` 下
2. **GPIO pin 冲突**：多个设备树节点不能占用同一引脚，需禁用冲突节点
3. **I2C 总线干扰**：LCD framebuffer 大面积刷新时可能干扰同板 I2C 总线，导致 GT911 I2C 读返回 -EIO
4. **GT911 IRQ 状态卡死**：中断处理必须每次写 0x00 清 0x814E，伪中断也要清
5. **GT911 地址选择**：复位释放时 INT 电平决定 I2C 地址（高=0x5D，低=0x14）
6. **TTY 版优于 FB 版**：实际产品中，写 tty 让内核管理 framebuffer 比直接 mmap 更稳定
7. **用户态联动优于内核耦合**：不同子系统（input/IIO）的业务逻辑放用户态，更安全更易维护

## 作者

Farewellove
