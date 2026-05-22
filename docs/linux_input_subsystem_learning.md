# Linux Input 子系统学习笔记

> 基于 i.MX6ULL IIO 传感器驱动项目中的 GT911 触摸屏驱动和 sensor_keys 驱动对照分析

---

## 一、Input 子系统是干什么的？

Linux Input 子系统是内核里专门处理**输入设备**（键盘、鼠标、触摸屏、游戏手柄、按键等）的一层抽象。

```
硬件驱动  →  Input 核心层  →  /dev/input/eventX  →  用户态（evdev 协议）
```

驱动只负责上报数据，Input 核心层帮你处理好：
- 设备节点创建
- 多路复用（`poll` / `select`）
- 事件缓冲队列
- 统一的 evdev 格式

用户态程序只需 `open("/dev/input/eventX")` + `read()` 即可获取标准格式的输入事件，不需要知道底层硬件细节。

---

## 二、项目中两个驱动的方式对比

|                     | sensor_keys                                | GT911                                    |
| ------------------- | ------------------------------------------ | ---------------------------------------- |
| **子系统**          | ❌ 不用 Input，用字符设备 `cdev`             | ✅ 用 Input 子系统                         |
| **数据出口**         | `/dev/sensor_collect` → 用户自定义 `read()` | `/dev/input/eventX` → 标准 evdev 协议   |
| **数据格式**         | 自己定义（2字节：LED+KEY）                   | 内核定义的标准 `struct input_event`     |
| **使用场景**         | 自己写的专用采集程序                          | Linux桌面 / tslib / Android 通用          |
| **代码复杂度**       | 较多（手动创建设备节点、file_operations）     | 较少（register/unregister 即可）         |
| **灵活性**           | 高（你想怎么传数据都行）                      | 受 evdev 协议约束                        |

---

## 三、GT911 驱动中的 Input API 详解

### 3.1 包含的头文件

```c
#include <linux/input.h>       // 核心 Input API
#include <linux/input/mt.h>    // 多点触摸 (Multi-Touch) 协议
#include <linux/input/touchscreen.h>  // 触摸屏辅助函数
```

### 3.2 分配 input_dev 结构体

```c
// 在 gt911_dev 结构体中定义
struct gt911_dev {
    struct input_dev *input;   // input 结构体
    // ... 其他成员
};

// probe 中分配
dev->input = devm_input_allocate_device(&client->dev);
```

`struct input_dev` 代表一个输入设备，包含：
- 设备名称
- 支持的事件类型
- 设备物理拓扑信息

### 3.3 设置设备属性

```c
dev->input->name = "gt911";
dev->input->id.bustype = BUS_I2C;
dev->input->dev.parent = &client->dev;
```

| 属性 | 含义 |
|------|------|
| `name` | 设备名，用户态可通过该字符串查找设备 |
| `id.bustype` | 总线类型（`BUS_I2C`、`BUS_SPI`、`BUS_USB` 等） |
| `id.vendor` / `id.product` / `id.version` | 厂商/产品/版本 ID |
| `dev.parent` | 父设备，用于 sysfs 关联 |

### 3.4 声明事件能力

```c
// 声明支持的绝对坐标事件，并设置范围
input_set_abs_params(dev->input, ABS_MT_POSITION_X, 0, dev->max_x, 0, 0);
input_set_abs_params(dev->input, ABS_MT_POSITION_Y, 0, dev->max_y, 0, 0);
```

更通用的写法也可以直接用 bit 操作：

```c
dev->input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);       // 支持按键 + 绝对坐标
set_bit(KEY_A, dev->input->keybit);                                 // 支持 KEY_A 键
set_bit(ABS_MT_POSITION_X, dev->input->absbit);                    // 支持 X 坐标
```

#### 常见事件类型

| 宏 | 含义 |
|----|------|
| `EV_KEY` | 按键/开关事件 |
| `EV_ABS` | 绝对坐标事件（触摸屏、摇杆） |
| `EV_REL` | 相对坐标事件（鼠标） |
| `EV_SYN` | 同步事件（帧结束标识） |

### 3.5 在中断中上报数据

```c
static irqreturn_t gt911_irq_handler(int irq, void *dev_id)
{
    // ... 读寄存器获取触摸数据 ...

    if (touch_num) {
        // 单点触摸按下
        input_mt_slot(dev->input, id);                                // 选择触摸点槽位
        input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, true); // 手指按下
        input_report_abs(dev->input, ABS_MT_POSITION_X, input_x);     // 上报 X
        input_report_abs(dev->input, ABS_MT_POSITION_Y, input_y);     // 上报 Y
    } else {
        // 触摸释放
        input_mt_slot(dev->input, id);
        input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, false);
    }

    input_mt_report_pointer_emulation(dev->input, true);  // 模拟指针（单点可用）
    input_sync(dev->input);                                // 帧同步：打包本帧全部事件

    // ...
}
```

#### 关键函数

| 函数 | 作用 |
|------|------|
| `input_report_abs(dev, code, value)` | 上报绝对坐标值 |
| `input_report_key(dev, code, value)` | 上报按键状态（1=按下，0=释放） |
| `input_report_rel(dev, code, value)` | 上报相对位移 |
| `input_sync(dev)` | **必须调用**，标记这一帧结束，打包发送到用户态 |
| `input_mt_slot(dev, slot)` | 选择当前操作的触摸点槽位 (MT Protocol B) |
| `input_mt_report_slot_state(dev, tool, active)` | 上报触摸点状态（active=1 按下 / 0 释放） |
| `input_mt_report_pointer_emulation(dev, use_count)` | 自动生成单点模拟（兼容老应用） |

### 3.6 注册设备

```c
ret = input_register_device(dev->input);
```

注册成功后，Linux 内核会自动创建：

```
/dev/input/eventX    ← 用户态通过此节点读取事件
/sys/class/input/inputX/  ← sysfs 接口
```

### 3.7 注销设备

```c
input_unregister_device(dev->input);
dev->input = NULL;
```

---

## 四、sensor_keys 为什么不用 Input？（对比学习）

sensor_keys 驱动用的是**字符设备（cdev）**方式：

```c
// 1. 分配设备号
alloc_chrdev_region(&sdev->devid, 0, SENSOR_CNT, SENSOR_NAME);

// 2. 初始化并添加 cdev
cdev_init(&sdev->cdev, &sensor_fops);
cdev_add(&sdev->cdev, sdev->devid, SENSOR_CNT);

// 3. 创建类 + 设备节点（生成 /dev/sensor_collect）
class_create(THIS_MODULE, SENSOR_NAME);
device_create(sdev->class, NULL, sdev->devid, NULL, SENSOR_NAME);

// 4. 用户态 read() 获取自定义格式的数据
static ssize_t sensor_read(struct file *filp, char __user *buf, ...) {
    status[0] = sdev->led_state;
    status[1] = gpio_get_value(sdev->key_gpio);
    copy_to_user(buf, status, sizeof(status));
    return sizeof(status);
}
```

**为什么这里用 cdev 而不用 Input？**

| 原因 | 说明 |
|------|------|
| 数据格式特殊 | 需要同时返回 LED 状态 + 按键状态（2 字节），Input 协议没有"LED 状态"事件 |
| 只有自己用 | 不需要兼容其他应用（如 tslib、X11），自定义协议更轻量 |
| 全双工 | 还需要 ioctl 控制 LED 开关，Input 协议仅单向上报 |
| 学习目的 | 作者可能同时想练习字符设备驱动 |

### 适用场景总结

| 场景 | 推荐方式 |
|------|---------|
| 键盘/鼠标/触摸屏/游戏手柄 | ✅ **Input 子系统** — 标准协议，兼容性好 |
| 简单按键（只有按下/弹起） | ✅ **Input 子系统** — 用 `input_report_key()` 即可 |
| 自定义传感器协议（温度、光照等） | ✅ **IIO 子系统** — 项目中 AP3216C 就是 IIO |
| 私有数据格式 + 双向通信 | ✅ **字符设备 (cdev)** — 完全自己控制 |
| 大量数据块传输（摄像头、音频） | ✅ **V4L2 / ALSA** 等专用子系统 |

---

## 五、核心数据结构

### 5.1 `struct input_dev`（关键成员）

```c
struct input_dev {
    const char *name;                        // 设备名称
    struct input_id id;                      // 总线类型/厂商/产品/版本
    unsigned long evbit[BITS_TO_LONGS(EV_CNT)];    // 支持的事件类型位图
    unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];   // 支持哪些按键
    unsigned long relbit[BITS_TO_LONGS(REL_CNT)];   // 支持哪些相对坐标
    unsigned long absbit[BITS_TO_LONGS(ABS_CNT)];   // 支持哪些绝对坐标

    struct input_absinfo absinfo[ABS_CNT];           // 绝对坐标的限幅/分辨率信息
    unsigned long mt_slots;                          // 多点触摸槽位数
    int (*open)(struct input_dev *dev);
    void (*close)(struct input_dev *dev);
    // ...
};
```

### 5.2 `struct input_event`（用户态读取的格式）

```c
struct input_event {
    struct timeval time;  // 时间戳
    __u16 type;           // 事件类型（EV_KEY, EV_ABS, EV_SYN...）
    __u16 code;           // 事件编码（KEY_A, ABS_X, BTN_TOUCH...）
    __s32 value;          // 事件值（0/1, 坐标值...）
};
```

每个事件固定 **16 字节**，用户态这样读：

```c
struct input_event ev;
read(fd, &ev, sizeof(ev));
```

---

## 六、多点触摸协议（MT Protocol B）

GT911 使用的是 **MT Protocol B**（带槽位追踪）。

```c
// 上报顺序必须固定：
input_mt_slot(dev, id);                    // ① 选择槽位（触摸点ID）
input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);  // ② 报状态
input_report_abs(dev, ABS_MT_POSITION_X, x);  // ③ 报 X
input_report_abs(dev, ABS_MT_POSITION_Y, y);  // ④ 报 Y
// 还可以报 ABS_MT_TOUCH_MAJOR（触摸面积）等
input_sync(dev);                           // ⑤ 同步（必须）

// 对于未激活的槽位：active=false, 表示该手指已抬起
```

| 特性 | Protocol A | Protocol B |
|------|------------|------------|
| 追踪方式 | 无追踪，一次报完所有点 | 带 slot 追踪，可识别手指 |
| 内核处理 | 简单 | 带 `ABS_MT_SLOT` 槽位管理 |
| 用户态 | 自己追踪手指 | 内核帮你追踪 |
| 调用函数 | `input_mt_report_pointer_emulation()` | `input_mt_slot()` + `input_mt_report_slot_state()` |

---

## 七、快速模板：写一个 Input 按键驱动

```c
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

static struct input_dev *input_dev;
static int irq_num;

static irqreturn_t btn_irq_handler(int irq, void *dev_id)
{
    int val = gpio_get_value(KEY_GPIO);   // 读 GPIO
    input_report_key(input_dev, KEY_ENTER, !val);  // 上报（假设低电平有效）
    input_sync(input_dev);
    return IRQ_HANDLED;
}

static int btn_probe(struct platform_device *pdev)
{
    int ret;

    // 1. 分配 input_dev
    input_dev = devm_input_allocate_device(&pdev->dev);

    // 2. 设置属性
    input_dev->name = "my_button";
    input_dev->id.bustype = BUS_HOST;
    input_dev->dev.parent = &pdev->dev;

    // 3. 声明能力
    input_dev->evbit[0] = BIT_MASK(EV_KEY);
    input_set_capability(input_dev, EV_KEY, KEY_ENTER);

    // 4. 注册
    ret = input_register_device(input_dev);

    // 5. 注册中断
    irq_num = gpio_to_irq(KEY_GPIO);
    ret = request_irq(irq_num, btn_irq_handler,
                      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                      "my_button", NULL);
    return ret;
}

static int btn_remove(struct platform_device *pdev)
{
    free_irq(irq_num, NULL);
    input_unregister_device(input_dev);
    return 0;
}
```

---

## 八、参考资源

- [Linux kernel 官方文档 - Input subsystem](https://www.kernel.org/doc/html/latest/input/)
- 项目中的实际代码：
  - GT911 触摸驱动：`kernel_drivers/gt911_ts/lcd.c`
  - Sensor Keys 字符设备驱动：`kernel_drivers/sensor_keys/sensor_driver.c`
  - AP3216C IIO 驱动：`kernel_drivers/ap3216c_iio/ap3216c.c`（另一种子系统）
