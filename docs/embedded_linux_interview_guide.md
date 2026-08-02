# 嵌入式 Linux 驱动面试背诵手册

> 基于 i.MX6ULL + Linux 4.1.15 四驱动综合项目实战整理
>
> 项目地址：`i.MX6ULL-IIO-LCD` | 作者：Farewellove

---

## 目录

1. [项目一句话介绍](#1-项目一句话介绍)
2. [硬件平台速记](#2-硬件平台速记)
3. [四大驱动核心知识点](#3-四大驱动核心知识点)
4. [Linux 内核子系统对比](#4-linux-内核子系统对比)
5. [面试高频问答 30 题](#5-面试高频问答-30-题)
6. [关键代码模板速记](#6-关键代码模板速记)
7. [调试方法论](#7-调试方法论)
8. [设备树要点](#8-设备树要点)
9. [简历项目描述模板](#9-简历项目描述模板)

---

## 1. 项目一句话介绍

> 在 i.MX6ULL ARM Cortex-A7 平台上，从零完成 **4 个 Linux 内核驱动**（I2C/SPI/Platform）+ **7 个用户态测试程序**的 bring-up，覆盖 **Input、IIO、Framebuffer、字符设备** 四个子系统，实现了 GT911 触摸屏触发 AP3216C 光传感器与 ICM20608 六轴 IMU 的**多子系统用户态联动**。

---

## 2. 硬件平台速记

| 项目 | 内容 |
|------|------|
| **SoC** | i.MX6ULL (NXP, ARM Cortex-A7, 单核, 800MHz) |
| **开发板** | 正点原子 ALPHA V2.4 |
| **内核版本** | Linux 4.1.15 |
| **LCD** | 7 寸 1024×600 RGB, 24bit, 51.2MHz pixel clock |
| **交叉编译器** | `arm-linux-gnueabihf-gcc` |
| **启动方式** | TFTP 加载内核 + dtb, NFS 挂载 rootfs |

### 硬件连接一览

| 设备 | 总线 | 关键引脚 |
|------|------|---------|
| AP3216C 光传感器 | I2C1, 0x1E | — |
| ICM20608 六轴 IMU | SPI3 (ecspi3), CS0 | MODE0, 8MHz |
| GT911 触摸屏 | I2C2, 0x5D | INT=GPIO1_IO09, RST=GPIO5_IO09 |
| LED + KEY | Platform 设备 | LED=GPIO1_IO03, KEY=GPIO1_IO18 |

---

## 3. 四大驱动核心知识点

### 3.1 GT911 触摸屏驱动 — Input 子系统 + I2C

**一句话**：I2C 电容触摸驱动，支持 5 点触摸，使用 Input 子系统的 MT Protocol B 上报多点坐标。

```
技术栈：I2C → threaded IRQ → Input MT Protocol B → /dev/input/eventX
```

| 要点 | 细节 |
|------|------|
| **总线** | I2C2, 地址 0x5D（由复位时 INT 电平决定：高→0x5D, 低→0x14） |
| **寄存器特点** | **16 位大端地址**，不能用 `i2c_smbus_read_byte_data()`（只支持 8 位地址），必须用 `i2c_transfer()` |
| **I2C 读流程** | msg[0]: 写 2 字节寄存器地址 → msg[1]: 读 N 字节数据 |
| **I2C 写流程** | `kmalloc(2+len)` 拼地址+数据 → `i2c_transfer` → **之后**再 `kfree` |
| **中断类型** | `request_threaded_irq` + `IRQF_TRIGGER_FALLING` + `IRQF_ONESHOT` |
| **中断处理** | 读 0x814E（状态寄存器）→ 读 0x814F（坐标数据 8B/点）→ `input_mt_slot` 上报 → **写 0x00 清 0x814E** |
| **多点触摸** | MT Protocol B (slot-based), 5 slots, `INPUT_MT_DIRECT` |
| **关键坑** | 状态清除必须写 **0x814E** 不能写 0x8040，否则 INT 不释放 |
| **鲁棒性** | I2C 读重试 3 次 + 连续失败 >20 次触发 workqueue 硬件复位 |
| **RST 引脚** | GPIO5_IO09（SNVS 域），pinctrl 必须在 `&iomuxc_snvs` 下 |
| **INT 引脚** | GPIO1_IO09，pinctrl 在 `&iomuxc` 下 |

#### 面试必背：为什么用 threaded IRQ？

```c
request_threaded_irq(irq, NULL, gt911_irq_handler,
                     IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "gt911_ts", data);
```

- `handler=NULL`：不使用 hardirq 上半部，整个处理在线程上下文（下半部）执行
- **原因**：中断里要读 I2C（会休眠），hardirq 禁止休眠，必须 threaded
- `IRQF_ONESHOT`：中断处理完成前保持屏蔽，防止中断重入

#### 面试必背：GPIO descriptor API vs 旧 API

| | 旧 API | 新 API (gpiod) |
|---|---|---|
| 获取 | `of_get_named_gpio()` + `gpio_request()` | `devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW)` |
| 设置方向 | `gpio_direction_output()` | 获取时已指定 |
| 设备树 | `reset-gpios = <&gpio5 9 GPIO_ACTIVE_LOW>` | 同 |
| `_raw` API | 无 | `gpiod_direction_output_raw()` 直接控制物理电平 |

---

### 3.2 AP3216C 光传感器驱动 — IIO 子系统 + I2C

**一句话**：I2C 三通道光传感器 IIO 驱动，提供 ALS/PS/IR 的 sysfs 接口。

```
技术栈：I2C smbus → IIO framework → /sys/bus/iio/devices/iio:deviceX/
```

| 要点 | 细节 |
|------|------|
| **总线** | I2C1, 地址 0x1E, 寄存器 8 位地址 — 可用 `i2c_smbus_read_byte_data()` |
| **三通道** | ALS（环境光, 16bit）、PS（接近检测, 10bit）、IR（红外, 10bit） |
| **初始化** | 写 0x04 软件复位 → 等 50ms → 写 0x03 开启三通道 |
| **IIO channel 定义** | `IIO_LIGHT`, `IIO_PROXIMITY`, `IIO_INTENSITY`(IR) |
| **用户态接口** | `cat /sys/bus/iio/devices/iio:device0/in_illuminance_raw` |
| **关键函数** | `read_raw()` 回调：根据 `mask == IIO_CHAN_INFO_RAW` 按通道类型分发 |
| **内存管理** | `iio_device_alloc(sizeof(*data))` 同时分配 IIO dev + 私有数据 |
| **关键坑** | `struct iio_info` 必须是 **static const 全局变量**，不能是 probe 临时变量 |
| **关键坑** | `MODULE_DEVICE_TABLE(of, ...)` 必须写，否则 modprobe 不会自动加载 |

#### 面试必背：IIO 子系统核心概念

```
IIO 子系统层次：
  iio_device_alloc() → 分配 IIO 设备
  iio_priv()         → 取出私有数据指针
  iio_device_register() → 注册 → 自动创建 /sys/bus/iio/devices/iio:deviceX/
  iio_device_unregister() + iio_device_free() → 注销

用户态读取路径：
  cat /sys/bus/iio/devices/iio:deviceX/name     → 确认设备存在
  cat /sys/bus/iio/devices/iio:deviceX/in_xxx_raw → 读原始值
```

---

### 3.3 ICM20608 六轴 IMU 驱动 — IIO 子系统 + SPI

**一句话**：SPI 六轴传感器 IIO 驱动，提供 3 轴加速度 + 3 轴陀螺仪 + 温度的 sysfs 接口。

```
技术栈：SPI (MODE0) → IIO framework → 7 通道数据 → /sys/bus/iio/
```

| 要点 | 细节 |
|------|------|
| **总线** | SPI3 (ecspi3), CS0, MODE0 (CPOL=0, CPHA=0), max 1MHz |
| **SPI 模式配置** | `spi->mode = SPI_MODE_0; spi->bits_per_word = 8; spi->max_speed_hz = 1000000; spi_setup(spi)` |
| **读写协议** | bit7=1 读 (0x80), bit7=0 写 (0x7F); 用 `spi_write_then_read()` 一次完成 |
| **初始化** | 复位(0x80→PWR_MGMT_1) → 退出休眠(0x01) → 读 WHO_AM_I(0x75) 验证 → 配置量程/滤波 |
| **WHO_AM_I** | ICM20608G = 0xAF, ICM20608D = 0xAE |
| **量程** | 陀螺仪 ±2000°/s (0x18), 加速度 ±16g (0x18) |
| **7 个通道** | `IIO_ACCEL`×3(X/Y/Z, indexed), `IIO_ANGL_VEL`×3, `IIO_TEMP`×1 |

#### 面试必背：spi_write_then_read vs spi_write + spi_read

```c
// 正确：一次调用完成写命令+读数据，CS 自动管理
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg) {
    u8 tx = reg | 0x80;   // bit7=1 → 读
    u8 rx;
    spi_write_then_read(dev->spi, &tx, 1, &rx, 1);
    return rx;
}

// 错误：分开调用会导致 CS 在中间拉高，芯片状态机重置
```

#### 面试必背：传感器数据换算

| 通道 | 原始值 → 物理量 |
|------|----------------|
| 加速度 | `g = raw / 2048` (16g 量程, 16bit → 32768/16 = 2048) |
| 陀螺仪 | `°/s = raw / 16.4` (2000°/s 量程, 32768/2000 ≈ 16.4) |
| 温度 | `°C = raw / 326.8 + 25` |

---

### 3.4 Sensor Keys 驱动 — Platform 驱动 + 字符设备

**一句话**：Platform 驱动模型的 LED+按键字符设备，演示 cdev 全流程 + 中断消抖。

```
技术栈：Platform bus → cdev → GPIO → IRQ + timer 消抖 → /dev/sensor_collect
```

| 要点 | 细节 |
|------|------|
| **驱动模型** | Platform driver，通过 `compatible = "my,sensor_collect"` 匹配设备树 |
| **字符设备流程** | `alloc_chrdev_region` → `cdev_init` → `cdev_add` → `class_create` → `device_create` |
| **设备节点** | `/dev/sensor_collect` |
| **read** | 返回 2 字节：`[led_state, key_state]` |
| **write** | `'1'` 亮 / `'0'` 灭 LED |
| **消抖策略** | 中断 → 读电平确认非抖动 → `atomic_xchg` 防重入 → `disable_irq_nosync` → 启动 50ms timer → timer 回调中再次读电平确认 → `enable_irq` |

#### 面试必背：Platform 驱动匹配机制

```
设备树节点              →  内核展开为 platform_device
  compatible = "my,xxx"     match_table 匹配
                                ↓
                           platform_driver->probe()
```

```c
static const struct of_device_id sensor_of_match[] = {
    {.compatible = "my,sensor_collect"},  // ← 与设备树 compatible 完全一致
    {},
};
MODULE_DEVICE_TABLE(of, sensor_of_match);
```

#### 面试必背：为什么这个驱动不用 Input 而用字符设备？

| 原因 | 说明 |
|------|------|
| 数据格式特殊 | 需同时返回 LED 状态 + KEY 状态（2 字节），Input 协议无此事件类型 |
| 双向通信 | 需 ioctl/write 控制 LED，Input 仅支持单向（设备→主机）上报 |
| 专用场景 | 不兼容通用输入应用（tslib/X11），自定义协议更合适 |

---

## 4. Linux 内核子系统对比

### 4.1 Input vs IIO vs 字符设备

| 维度 | Input 子系统 | IIO 子系统 | 字符设备 (cdev) |
|------|-------------|-----------|----------------|
| **适用场景** | 键盘/鼠标/触摸屏/手柄 | 传感器（加速度/光/温度等） | 自定义协议设备 |
| **用户态接口** | `/dev/input/eventX` | `/sys/bus/iio/devices/iio:deviceX/` | 自定义 `/dev/xxx` |
| **数据方向** | 设备→主机（中断驱动） | 主机→设备（轮询读取） | 双向（read/write/ioctl） |
| **数据格式** | 标准 `struct input_event` (16B) | ASCII 文本 (`cat` 可读) | 自定义二进制格式 |
| **协议标准** | evdev 协议（通用） | IIO sysfs 标准 | 无标准，自行定义 |
| **本项目设备** | GT911 | AP3216C, ICM20608 | LED+KEY |

### 4.2 I2C vs SPI

| 维度 | I2C | SPI |
|------|-----|-----|
| **线数** | 2 线 (SCL + SDA) | 4 线 (SCLK + MOSI + MISO + CS) |
| **速度** | 标准 100k/400k, 最快 3.4M | 通常 1M~50M |
| **寻址** | 7/10 位设备地址 | CS 片选硬件寻址 |
| **全双工** | 半双工 | 全双工 |
| **多设备** | 同总线多设备（不同地址） | 同总线多设备（不同 CS） |
| **内核 API** | `i2c_transfer()` / `i2c_smbus_*()` | `spi_write()` / `spi_write_then_read()` |
| **本项目设备** | AP3216C, GT911 | ICM20608 |

### 4.3 中断上半部 vs 下半部

| | 上半部 (hardirq) | 下半部 (thread/workqueue/tasklet) |
|---|---|---|
| **执行上下文** | 中断上下文 | 进程上下文（可休眠） |
| **能否休眠** | ❌ 不能 | ✅ 可以 |
| **耗时要求** | 越快越好（微秒级） | 可以较长 |
| **典型操作** | 清中断标志、关中断 | I2C/SPI 读写、数据处理 |
| **本项目** | — | GT911: threaded IRQ; Sensor Keys: timer 回调 |

---

## 5. 面试高频问答 30 题

### 基础概念

**Q1: Linux 设备驱动有哪几种类型？**

> 字符设备（cdev）、块设备（block）、网络设备（net_device）。本项目涉及字符设备及其子类：Input 设备、IIO 设备。字符设备按字节流访问，通过 `file_operations` 提供 `open/read/write/ioctl` 接口。

**Q2: 字符设备驱动的注册流程？**

> 1. `alloc_chrdev_region(&devid, 0, count, name)` — 动态分配设备号
> 2. `cdev_init(&cdev, &fops)` — 初始化 cdev 并绑定 file_operations
> 3. `cdev_add(&cdev, devid, count)` — 注册到内核
> 4. `class_create(THIS_MODULE, name)` — 创建 /sys/class 下的类
> 5. `device_create(class, NULL, devid, NULL, name)` — 自动生成 /dev/xxx 节点
>
> 注销顺序严格相反：device_destroy → class_destroy → cdev_del → unregister_chrdev_region

**Q3: `copy_from_user` / `copy_to_user` 的作用？**

> 内核空间与用户空间地址隔离，不能直接通过指针传递数据。这两个函数在 `#include <linux/uaccess.h>` 中定义，内部会检查用户态指针的合法性，防止内核访问非法地址导致 oops。返回值 0 表示成功，非 0 表示未拷贝完的字节数。

**Q4: `container_of` 宏的原理？**

> 通过结构体成员的地址反推出结构体本身的地址。原理：`(type *)( (char *)ptr - offsetof(type, member) )`。项目中在 `sensor_open()` 中通过 `inode->i_cdev` 反推 `sensor_dev` 结构体。

**Q5: `kmalloc` vs `vmalloc` vs `devm_kzalloc`？**

| | kmalloc | vmalloc | devm_kzalloc |
|---|---|---|---|
| 内存区 | 物理连续 | 虚拟连续 | 物理连续 |
| 性能 | 快 | 慢（需建页表） | 同 kmalloc |
| 释放 | 需手动 kfree | 需手动 vfree | **设备移除时自动释放** |
| 适用 | 小内存/DMA | 大块内存 | **驱动开发推荐** |

**Q6: Linux 内核中用户态和内核态如何通信？**

> 1. **系统调用** — read/write/ioctl，最常用
> 2. **sysfs** — `/sys` 下的文件节点（IIO 就是这种方式）
> 3. **procfs** — `/proc` 下的文件节点
> 4. **netlink** — socket 方式，用于 udev 等
> 5. **mmap** — 内存映射，如 framebuffer

### 设备树

**Q7: 设备树 (Device Tree) 的作用是什么？**

> 将硬件描述（板级信息）从内核代码中分离出来。内核启动时由 bootloader 将 `.dtb` 传给内核（本项目通过 TFTP），内核解析后生成 `platform_device`，与 `platform_driver` 通过 `compatible` 属性匹配，匹配成功则调用 probe。

**Q8: compatible 属性的匹配规则？**

> `compatible = "vendor,model"`。驱动中 `of_match_table` 定义的 `.compatible` 必须与设备树中节点的 `compatible` 字符串完全一致，内核才会调用 probe。本项目示例：
> - GT911: `"sensor_collect,gt911"`
> - AP3216C: `"sensor_collect,ap3216c"`
> - Sensor Keys: `"my,sensor_collect"`

**Q9: iomuxc vs iomuxc_snvs 的区别？**

> i.MX6ULL 有两个独立的 pin controller：
>
> | Pin Controller | 基地址 | 管辖 |
> |---|---|------|
> | `&iomuxc` | `0x020E0000` | GPIO1~GPIO4 |
> | `&iomuxc_snvs` | `0x02290000` | GPIO5 + BOOT_MODE 引脚 |
>
> **SNVS 域的引脚（如 GPIO5_IO09）必须在 `&iomuxc_snvs` 下配置 pinctrl，放在 `&iomuxc` 下是静默无效的**。DTC 不报错，但 pin controller 不认识这个引脚。

**Q10: pinctrl-0 可以引用跨 pin controller 的 group 吗？**

> 可以。`pinctrl-0 = <&ts_int_pin>, <&ts_reset_pin>;` 中两个 phandle 分别指向不同 pin controller 下的 group，内核 pinctrl 框架会根据每个 group 所属的 controller 自动路由。但不允许同一个 pin group 被多个设备节点同时引用（会报 conflict）。

### I2C

**Q11: i2c_transfer 和 i2c_smbus_read_byte_data 的区别？**

> `i2c_smbus_read_byte_data()` 只发 **1 字节**寄存器地址，适用于寄存器地址 ≤ 8bit 的设备（如 AP3216C）。GT911 的寄存器地址是 **16 位大端序**，必须用 `i2c_transfer()` 构造两条消息：msg[0] 写 2 字节地址，msg[1] 读数据。

**Q12: I2C 设备地址是如何确定的？**

> - 芯片厂商标定（如 AP3216C 固定 0x1E）
> - 硬件引脚选择（如 GT911：复位释放时 INT 高→0x5D, INT 低→0x14）
> - 设备树 `reg = <0x5d>;` 属性声明

### SPI

**Q13: SPI 四种模式的区别？**

> | Mode | CPOL (时钟极性) | CPHA (时钟相位) |
> |------|-----------------|-----------------|
> | MODE0 (0,0) | 空闲低 | 第1边沿采样 |
> | MODE1 (0,1) | 空闲低 | 第2边沿采样 |
> | MODE2 (1,0) | 空闲高 | 第1边沿采样 |
> | MODE3 (1,1) | 空闲高 | 第2边沿采样 |
>
> ICM20608 使用 MODE0，通过 `spi->mode = SPI_MODE_0; spi_setup(spi)` 配置。

**Q14: spi_write_then_read 的优势？**

> 一次函数调用完成 "写命令 + 读数据"，**CS 在整条命令期间保持拉低**。如果分开用 `spi_write` + `spi_read`，CS 会在中间拉高，可能导致 SPI 设备状态机重置、数据丢失。

### Input 子系统

**Q15: input_event 结构体的字段含义？**

```c
struct input_event {
    struct timeval time;  // 内核时间戳
    __u16 type;           // EV_KEY / EV_ABS / EV_REL / EV_SYN
    __u16 code;           // 具体编码 (BTN_TOUCH / ABS_X / SYN_REPORT)
    __s32 value;          // 值 (0/1 或坐标值)
};
// 每个事件固定 16 字节
```

**Q16: input_sync() 为什么必须调用？**

> input_sync() 发送 `EV_SYN / SYN_REPORT` 事件，标记一帧的结束。内核将此帧之前的所有事件打包成一组发给用户态。不调用 input_sync() 的话，用户态 `read()` 会被阻塞，收不到任何事件。

**Q17: MT Protocol A vs Protocol B？**

> | | Protocol A | Protocol B (本项目) |
> |---|---|---|
> | 追踪方式 | 无追踪，一次报所有点 | 带 slot ID 追踪每个手指 |
> | 内核接口 | `input_mt_report_pointer_emulation()` | `input_mt_slot()` + `input_mt_report_slot_state()` |
> | 用户态 | 自己追踪手指抬起 | 内核帮你追踪 |
>
> GT911 使用 Protocol B：`input_mt_slot(dev, id)` → `input_mt_report_slot_state(dev, MT_TOOL_FINGER, active)` → `input_report_abs(X/Y)` → `input_mt_sync_frame()` → `input_sync()`。

**Q18: input_set_capability vs evbit/keybit 位图直接操作？**

```c
// 方式一：高级 API
input_set_capability(idev, EV_KEY, BTN_TOUCH);
input_set_abs_params(idev, ABS_MT_POSITION_X, 0, 1024, 0, 0);

// 方式二：位图直接操作（更底层）
idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
set_bit(BTN_TOUCH, idev->keybit);
```
> 推荐使用高级 API，更清晰且自动处理位宽问题。

### IIO 子系统

**Q19: IIO 通道定义中 indexed 和 modified 的含义？**

> - **indexed=1**: 同一类型有多个实例，用 `.channel` 序号区分（如加速度 X=0, Y=1, Z=2）
> - **modified=1**: 通道有修饰符，用 `.channel2` 区分（如红外强度 `IIO_MOD_LIGHT_IR`）
> - 对应 sysfs 节点名：`in_accel0_raw`（indexed）vs `in_intensity_ir_raw`（modified）

**Q20: IIO 的 info_mask_separate 是什么意思？**

> 指定该通道支持的属性。`BIT(IIO_CHAN_INFO_RAW)` 表示支持原始值读取 → 生成 `in_xxx_raw` 节点。`BIT(IIO_CHAN_INFO_SCALE)` 表示支持 scale → 生成 `in_xxx_scale` 节点。用户态读取对应 sysfs 文件时，内核回调 `read_raw()` 函数。

### 中断

**Q21: request_irq vs request_threaded_irq vs devm_request_threaded_irq？**

> | | request_irq | request_threaded_irq | devm_request_threaded_irq |
> |---|---|---|---|
> | 上半部 | handler（中断上下文） | handler 可为 NULL | 同左 |
> | 下半部 | 需自己实现 | thread_fn（进程上下文） | 同左 |
> | 释放 | 需手动 free_irq | 需手动 free_irq | **设备 remove 时自动释放** |
>
> GT911 使用 `devm_request_threaded_irq(dev, irq, NULL, gt911_irq_handler, ...)`，hardirq 为空，全部在 thread_fn 中处理（因为要 I2C 通信，需休眠）。

**Q22: 按键消抖的软件实现方法？**

> 本项目的 Sensor Keys 驱动使用 **中断 + 定时器** 消抖方案：
> 1. 中断来临 → 立即读 GPIO 电平，若已恢复高电平则为抖动 → 忽略
> 2. 若电平确实为低 → `atomic_xchg(&debouncing, 1)` 防重入 → `disable_irq_nosync` → 启动 50ms timer
> 3. 50ms 后 timer 回调 → 再次读电平 → 仍为低则为真实按下 → 翻转 LED → `atomic_set(&debouncing, 0)` → `enable_irq`
>
> 备选方案：在硬件上加 RC 滤波电路。

### Framebuffer

**Q23: Framebuffer 的工作原理？**

> Framebuffer 是 Linux 内核提供的显存抽象层。用户态通过 `/dev/fb0` 访问物理显存：
> 1. `open("/dev/fb0")` → `ioctl(FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO)` 获取分辨率、bpp、行长度等参数
> 2. `mmap(NULL, smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)` 映射显存到用户空间
> 3. 直接写映射后的内存区域 → LCD 控制器自动刷新显示

**Q24: 16bpp RGB565 vs 32bpp 像素格式的区别？**

> | | 16bpp (RGB565) | 32bpp (XRGB8888) |
> |---|---|---|
> | R | 5 bit (高 5 位) | 8 bit |
> | G | 6 bit | 8 bit |
> | B | 5 bit (低 5 位) | 8 bit |
> | 每像素字节 | 2 | 4 |
>
> 颜色转换需根据 `fb_var_screeninfo` 中 `red/green/blue` 的 `.offset` 和 `.length` 动态计算，**不能硬编码**，否则颜色会错乱。本项目 `color_to_pixel()` 函数实现了通用的 0x00RRGGBB → 本地像素格式转换。

### 内核机制

**Q25: mutex vs spinlock 的使用场景？**

> | | mutex | spinlock |
> |---|---|---|
> | 上下文 | 进程上下文（可休眠） | 任意上下文 |
> | 是否休眠 | 获取不到时**休眠**等待 | 获取不到时**自旋**等待 |
> | 开销 | 较重（上下文切换） | 较轻（CPU 忙等） |
> | 适用 | I2C/SPI 通信、长临界区 | 中断上下文、短临界区 |
>
> 本项目 GT911 和 AP3216C 使用 mutex（保护 I2C 读写，会休眠），不可在 hardirq 中使用。

**Q26: workqueue 的作用？GT911 中如何使用？**

> workqueue 将工作推迟到进程上下文执行（可休眠）。GT911 中用于 **硬件故障恢复**：
> - IRQ 中 I2C 连续失败 20 次以上 → `schedule_work(&data->reset_work)`
> - reset_work 中：`disable_irq` → mutex_lock → 硬件复位时序 → 读 PID 验证 → 清零计数 → mutex_unlock → `enable_irq`
> - **不能在中断中直接复位**（耗时太长，会阻塞其他中断），所以用 workqueue 异步执行。

**Q27: module_init / module_exit 的调用时机？**

> - `module_init(init_fn)` → `insmod` 时调用
> - `module_exit(exit_fn)` → `rmmod` 时调用
> - 与 `__init` / `__exit` 属性配合：`__init` 标记的函数在初始化完成后释放内存

**Q28: MODULE_LICENSE("GPL") 的作用？**

> 声明模块许可证为 GPL。非 GPL 模块无法调用内核中标记为 `EXPORT_SYMBOL_GPL` 的符号（如某些内核内部函数）。不声明会导致内核被 "tainted"（污染），且无法使用部分内核 API。

### 构建部署

**Q29: 交叉编译内核模块的 Makefile 怎么写？**

```makefile
KERNELDIR := /path/to/linux-kernel-source
obj-m += my_driver.o
my_driver-objs := path/to/source.o

all:
    make -C $(KERNELDIR) M=$(PWD) modules
clean:
    make -C $(KERNELDIR) M=$(PWD) clean
```
> - `-C $(KERNELDIR)`：切换到内核源码目录执行 make
> - `M=$(PWD)`：指定模块源码目录
> - `obj-m`：需要编译的内核模块
> - `xxx-objs`：模块由哪些 .o 文件链接而成

**Q30: 修改设备树后如何确认生效？**

> 1. 编译：`make dtbs` → 生成 `.dtb`
> 2. 部署到正确的启动路径（本项目是 TFTP 目录）
> 3. 板端验证：
> ```bash
> find /proc/device-tree -name "gt911@5d"       # 确认节点存在
> cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep TAMPER9  # 确认 pinctrl
> ```
> **常见坑**：部署路径不对（TFTP vs NFS vs eMMC），改了 DTS 但板子加载的是旧 DTB。

---

## 6. 关键代码模板速记

### 6.1 I2C 驱动模板

```c
#include <linux/i2c.h>
#include <linux/module.h>

static int my_probe(struct i2c_client *client, const struct i2c_device_id *id) {
    struct my_data *data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    i2c_set_clientdata(client, data);
    // ... 初始化 ...
    return 0;
}

static int my_remove(struct i2c_client *client) {
    // ... 清理 ...
    return 0;
}

static const struct of_device_id my_of_match[] = {
    {.compatible = "vendor,device"},
    {},
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct i2c_driver my_driver = {
    .probe    = my_probe,
    .remove   = my_remove,
    .driver   = { .name = "my_dev", .of_match_table = my_of_match },
    .id_table = my_id_table,
};
module_i2c_driver(my_driver);  // 宏，展开为 module_init/module_exit + i2c_add_driver/i2c_del_driver
MODULE_LICENSE("GPL");
```

### 6.2 SPI 驱动模板

```c
#include <linux/spi/spi.h>

static int my_probe(struct spi_device *spi) {
    struct my_data *data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;
    spi_setup(spi);
    spi_set_drvdata(spi, data);
    return 0;
}

static struct spi_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = { .name = "my_dev", .of_match_table = my_of_match },
    .id_table = my_id_table,
};
module_spi_driver(my_driver);
MODULE_LICENSE("GPL");
```

### 6.3 Input 设备注册模板

```c
struct input_dev *idev = devm_input_allocate_device(dev);
idev->name = "My Touchscreen";
idev->id.bustype = BUS_I2C;

input_set_capability(idev, EV_KEY, BTN_TOUCH);
input_set_abs_params(idev, ABS_MT_POSITION_X, 0, 1024, 0, 0);
input_set_abs_params(idev, ABS_MT_POSITION_Y, 0, 600,  0, 0);
input_mt_init_slots(idev, 5, INPUT_MT_DIRECT);
input_register_device(idev);

// 中断中上报：
input_mt_slot(idev, id);
input_mt_report_slot_state(idev, MT_TOOL_FINGER, true);
input_report_abs(idev, ABS_MT_POSITION_X, x);
input_report_abs(idev, ABS_MT_POSITION_Y, y);
input_mt_sync_frame(idev);
input_sync(idev);
```

### 6.4 IIO 设备注册模板

```c
struct iio_dev *indio_dev = iio_device_alloc(sizeof(*data));
struct my_data *data = iio_priv(indio_dev);

indio_dev->name = "my_sensor";
indio_dev->channels = my_channels;
indio_dev->num_channels = ARRAY_SIZE(my_channels);
indio_dev->info = &my_iio_info;
indio_dev->modes = INDIO_DIRECT_MODE;

iio_device_register(indio_dev);

// read_raw 回调：
static int my_read_raw(struct iio_dev *indio_dev,
                       struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask) {
    if (mask == IIO_CHAN_INFO_RAW) {
        *val = read_sensor();
        return IIO_VAL_INT;
    }
    return -EINVAL;
}
```

### 6.5 中断 + 消抖模板

```c
// 方式一：threaded IRQ（适合 I2C/SPI 通信）
devm_request_threaded_irq(dev, irq, NULL, my_thread_fn,
                          IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                          "my_dev", data);

// 方式二：普通 IRQ + 软件定时器消抖（适合简单 GPIO）
request_irq(irq, my_handler, IRQF_TRIGGER_FALLING, "my_irq", data);
// handler 中：
disable_irq_nosync(irq);
mod_timer(&timer, jiffies + msecs_to_jiffies(50));
// timer 回调中：
if (gpio_get_value(gpio) == 0) { /* 确认按下 */ }
enable_irq(irq);
```

### 6.6 Framebuffer 用户态操作模板

```c
int fd = open("/dev/fb0", O_RDWR);
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

uint8_t *fb = mmap(NULL, finfo.smem_len, PROT_READ|PROT_WRITE,
                   MAP_SHARED, fd, 0);

// 像素写入 (32bpp)：
uint32_t *p = (uint32_t *)fb;
int offset = y * finfo.line_length / 4 + x;
p[offset] = color;

munmap(fb, finfo.smem_len);
close(fd);
```

---

## 7. 调试方法论

### 7.1 驱动调试链式排查法

> 当 probe 流程中某步失败时，必须从链条最前端排查，不能跳跃：

```
I2C 读失败 (-EIO)
  → 芯片是否处于复位？(RST 电平)
    → GPIO 能否正常控制？(用旧 API 对照)
      → pinctrl 是否生效？(/sys/kernel/debug/pinctrl/*/pinmux-pins)
        → DTS 是否正确？(pin controller 域是否正确)
          → DTB 是否部署到位？(TFTP/NFS/eMMC 路径)
            → 是否有 pinctrl 冲突？(同一引脚被多个设备引用)
```

### 7.2 常用调试命令

```bash
# 设备树
find /proc/device-tree -name "*gt911*"         # 确认节点存在
cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep -E "TAMPER|GPIO1_IO09"

# 模块
lsmod                              # 已加载模块
insmod / rmmod / modprobe          # 加载/卸载模块
dmesg | tail -50                   # 查看内核日志

# Input
cat /proc/bus/input/devices        # 列出所有 input 设备
hexdump /dev/input/event1          # 原始查看事件

# IIO
ls /sys/bus/iio/devices/           # IIO 设备列表
cat /sys/bus/iio/devices/iio:device0/name  # 设备名
cat /sys/bus/iio/devices/iio:device0/in_illuminance_raw

# Framebuffer
cat /proc/fb                        # fb 设备列表
fbset -i                            # fb 详细参数
dmesg | grep -i fb                  # fb 驱动日志

# GPIO / 中断
cat /sys/kernel/debug/gpio          # GPIO 状态
cat /proc/interrupts | grep gt911   # 中断计数
```

### 7.3 本项目踩过的关键坑

| 序号 | 问题 | 根因 | 教训 |
|------|------|------|------|
| 1 | RST 引脚始终低电平 | GPIO5_IO09 是 SNVS 域，pinctrl 放在 `&iomuxc` 下静默无效 | SNVS 引脚必须放 `&iomuxc_snvs` |
| 2 | DTS 改了不生效 | dtb 部署到了 NFS 但板子 TFTP 启动 | 确认开机加载路径 |
| 3 | pinctrl 冲突 | 同一 pinctrl group 被两个节点引用 | 只保留实际使用该引脚的节点 |
| 4 | I2C 写后 use-after-free | kmalloc 的 buf 在 i2c_transfer 之前 kfree 了 | kfree 放在 i2c_transfer 之后 |
| 5 | GT911 INT 卡死不触发 | 清状态写成了 GT_CTRL_REG(0x8040) | 必须写 GT_GSTID_REG(0x814E) |
| 6 | iio_info 崩溃 | 用的是 probe 的局部变量 | 必须 static const 全局 |
| 7 | MODULE_DEVICE_TABLE 遗漏 | 驱动加载了但 probe 不调用 | 必须导出匹配表 |

---

## 8. 设备树要点

### 8.1 i.MX6ULL 设备树结构速记

```
/ {
    aliases { ... }
    chosen { stdout-path = &uart1; }
    memory { reg = <0x80000000 0x20000000>; }  // 512MB DDR3 @ 0x80000000

    soc {
        i2c1: i2c@021a0000 { ... }    // I2C1 控制器
        i2c2: i2c@021a4000 { ... }    // I2C2 控制器
        ecspi3: spi@02010000 { ... }  // SPI3 控制器
        lcdif: lcdif@021c8000 { ... } // LCD 控制器

        iomuxc: iomuxc@020e0000 { ... }      // 普通引脚复用 (GPIO1-4)
        iomuxc_snvs: iomuxc-snvs@02290000 { ... }  // SNVS 域 (GPIO5)
    };
};
```

### 8.2 DTS 节点标准写法

```dts
// I2C 设备
&i2c2 {
    gt911@5d {
        compatible = "sensor_collect,gt911";
        reg = <0x5d>;
        pinctrl-0 = <&ts_int_pin>, <&ts_reset_pin>;  // 跨 pinctrl 引用
        reset-gpios = <&gpio5 9 GPIO_ACTIVE_LOW>;
        irq-gpios   = <&gpio1 9 GPIO_ACTIVE_LOW>;
        interrupt-parent = <&gpio1>;
        interrupts = <9 IRQ_TYPE_EDGE_FALLING>;
    };
};

// Platform 设备
/ {
    sensor_collect {
        compatible = "my,sensor_collect";
        key-gpio = <&gpio1 18 GPIO_ACTIVE_LOW>;
        led-gpio = <&gpio1 3 GPIO_ACTIVE_LOW>;
    };
};

// LCD 时序
&lcdif {
    display0: display {
        bits-per-pixel = <32>;
        bus-width = <24>;
        display-timings {
            native-mode = <&timing0>;
            timing0: timing0 {
                clock-frequency = <51200000>;   // 51.2MHz
                hactive = <1024>; vactive = <600>;
                hfront-porch = <160>; hback-porch = <140>; hsync-len = <20>;
                vfront-porch = <12>;  vback-porch = <20>;  vsync-len = <3>;
                hsync-active = <0>; vsync-active = <0>;
                de-active = <1>; pixelclk-active = <0>;
            };
        };
    };
};
```

---

## 9. 简历项目描述模板

### 简短版（一行）

> 基于 i.MX6ULL + Linux 4.1.15 完成 GT911/I2C 触摸、AP3216C/I2C 光感、ICM20608/SPI 六轴 IMU 及按键 LED 共 4 个内核驱动的 bring-up，覆盖 Input/IIO/Framebuffer/字符设备四个子系统。

### 详细版（项目描述段落，适合面试展开讲）

> **项目：i.MX6ULL 多传感器 Linux 驱动综合开发**
>
> - 在正点原子 i.MX6ULL ALPHA 开发板上，独立完成 4 个 Linux 内核驱动的 bring-up：
>   - **GT911 触摸屏**（I2C + Input 子系统）：支持 5 点触摸，MT Protocol B，threaded IRQ + workqueue 故障恢复，16 位寄存器大端序 I2C 通信
>   - **AP3216C 光传感器**（I2C + IIO 子系统）：ALS/PS/IR 三通道，IIO channel 定义与 sysfs 接口
>   - **ICM20608 六轴 IMU**（SPI + IIO 子系统）：加速度/陀螺仪/温度共 7 通道，SPI MODE0 通信，spi_write_then_read 协议
>   - **LED+按键**（Platform 驱动 + 字符设备）：软件定时器消抖、原子变量防重入
> - 编写 **fb_draw 绘图模块**：mmap framebuffer 直接操控显存，支持 16/32bpp 像素格式自适应转换，自建 8×16 点阵字体渲染
> - 实现 **用户态多子系统联动 demo**：触摸屏 input event 触发后，自动读取 IIO sysfs 传感器数据并显示到 LCD 屏幕
> - 完成 **设备树配置**：pinctrl（含 SNVS 域）、I2C/SPI 总线、LCD 时序参数（51.2MHz pixel clock）
> - 定位并解决多个底层问题：GPIO SNVS 域 pinctrl 静默无效、I2C 总线 LCD 干扰导致 -EIO、GT911 INT 状态清除寄存器错误、pinctrl 跨设备冲突等

---

## 附录：快速自检清单

面试前对照自检，确保每个点都能说清楚：

- [ ] 能说出 Linux 设备驱动的三种分类及本项目各驱动属于哪种
- [ ] 能画出字符设备注册流程（alloc → cdev_init → cdev_add → class_create → device_create）
- [ ] 能解释 I2C 设备树匹配 → probe 的完整流程
- [ ] 能说出 Input 子系统中 input_event 的结构、input_sync 的作用
- [ ] 能说出 MT Protocol B 的上报顺序（slot → state → abs → sync_frame → sync）
- [ ] 能说出 IIO 子系统中 read_raw 回调的作用和参数含义
- [ ] 能解释 threaded IRQ 的使用场景（I2C/SPI 通信需休眠）
- [ ] 能解释为什么 GT911 用 i2c_transfer 而 AP3216C 用 smbus API
- [ ] 能说清楚 iomuxc vs iomuxc_snvs 的区别
- [ ] 能说出 mutex vs spinlock 的适用场景
- [ ] 能说出 kmalloc/devm_kzalloc/kfree 的区别
- [ ] 能说出 framebuffer 用户态操作的三个步骤（open → ioctl + mmap → 写显存）
- [ ] 能说出交叉编译内核模块的 Makefile 写法（M= 和 -C 的含义）
- [ ] 能说清楚本项目中至少 3 个踩坑经验及其排查思路
- [ ] 能流畅地用 STAR 法则描述项目经历（背景→任务→行动→结果）

---

> 文档生成时间：2026-07-07 | 祝你面试顺利！🎯
