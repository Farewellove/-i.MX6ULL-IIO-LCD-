/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/12 22:00:29
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/27 23:04:05
 * @Description: GT911 电容触摸屏 I2C 驱动，支持 5 点触摸
 *               硬件：正点原子 i.MX6ULL ALPHA V2.4 + 7寸 1024x600 LCD
 *               I2C2, 地址 0x5D, INT=GPIO1_IO09, RST=GPIO5_IO09
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/input/touchscreen.h>

/*
 * GT911 寄存器（GT9147 兼容，16 位地址大端序）
 * 寄存器地址空间分为两段：
 *   0x8040-0x80FF: 配置段（R/W）
 *   0x8140-0x817F: 数据段（触摸坐标、状态等）
 */
#define GT_CTRL_REG   0X8040  /* 命令寄存器，写 0x00 表示主机已读完坐标 */
#define GT_MODSW_REG  0X804D  /* 模式切换寄存器 */
#define GT_CFGS_REG   0X8047  /* 配置起始地址（184 字节） */
#define GT_CHECK_REG  0X80FF  /* 配置校验和寄存器 */
#define GT_PID_REG    0X8140  /* 产品 ID 寄存器（4 字节，例: '9','1','1'） */

#define GT_GSTID_REG  0X814E  /* 触摸状态寄存器: bit7=数据就绪, bit3~0=触摸点数 */
#define GT_TP_REG     0X814F  /* 第一个触摸点数据地址，后续点连续排列 */
#define MAX_SUPPORT_POINTS  5  /* 最多 5 点电容触摸 */

/*
 * 每个触摸点 8 字节：
 *   [0]       track_id (低 4 位)
 *   [1:2]     X 坐标 (little-endian)
 *   [3:4]     Y 坐标 (little-endian)
 *   [5:6]     触摸面积
 *   [7]       reserved
 */
#define GT911_STATUS_BUF_READY  0x80  /* 状态寄存器 bit7 掩码 */
#define GT911_TOUCH_POINT_SIZE  8     /* 每点 8 字节 */

struct gt911_data
{
    struct i2c_client *client;       /* 挂载的 I2C 设备 */
    struct gpio_desc *reset_gpio;    /* RST 引脚 (GPIO5_IO09, SNVS 域, ACTIVE_LOW) */
    struct gpio_desc *irq_gpio;      /* INT 引脚 (GPIO1_IO09, ACTIVE_LOW) */
    int irq;                         /* 中断号 (由 gpiod_to_irq 转换) */
    struct input_dev *input_dev;     /* 注册到 input 子系统的设备 */
};

/*
 * I2C 读 —— GT911 寄存器地址是 16 位大端序，不能直接用 smbus API。
 * 先发 2 字节地址，再读 len 字节数据，两条消息用一次 i2c_transfer 完成。
 */
static int gt911_i2c_read(struct gt911_data *data, u16 reg, u8 *buf, int len)
{
    struct i2c_client *client = data->client;
    u8 reg_buf[2];
    struct i2c_msg msgs[2];
    int ret;

    /* 地址高字节在前（大端） */
    reg_buf[0] = (reg >> 8) & 0xFF;
    reg_buf[1] = reg & 0xFF;

    /* 第一条消息：写 2 字节寄存器地址 */
    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;               /* 写标志 */
    msgs[0].len   = 2;
    msgs[0].buf   = reg_buf;

    /* 第二条消息：读回数据 */
    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD;        /* 读标志 */
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret == 2)                    /* 两条消息都成功 */
        return 0;
    else if (ret < 0)                /* 传输错误，返回负错误码 */
        return ret;
    else                             /* 部分成功（如只发了地址但没读到数据） */
        return -EIO;
}

/*
 * I2C 写 —— 将 16 位地址 + 数据拼成一个连续缓冲区，一条消息发出。
 * 注意 kmalloc 分配的临时缓冲区在用完后必须 kfree。
 */
static int gt911_i2c_write(struct gt911_data *data, u16 reg, u8 *buf, int len)
{
    struct i2c_client *client = data->client;
    u8 *tx_buf;
    struct i2c_msg msg;
    int ret;

    /* +2 是因为要把 16 位寄存器地址也打包进去 */
    tx_buf = kmalloc(2 + len, GFP_KERNEL);
    if (!tx_buf)
        return -ENOMEM;

    tx_buf[0] = (reg >> 8) & 0xFF;   /* 地址高字节 */
    tx_buf[1] = reg & 0xFF;          /* 地址低字节 */
    memcpy(&tx_buf[2], buf, len);    /* 数据从下标 2 开始 */

    msg.addr  = client->addr;
    msg.flags = 0;
    msg.buf   = tx_buf;
    msg.len   = 2 + len;

    ret = i2c_transfer(client->adapter, &msg, 1);
    kfree(tx_buf);                   /* 传输完成后立即释放 */

    if (ret == 1)
        return 0;
    else if (ret < 0)
        return ret;
    else
        return -EIO;
}

/* 写单个字节到 16 位地址的寄存器 */
static inline int gt911_i2c_write_byte(struct gt911_data *data, u16 reg, u8 val)
{
    return gt911_i2c_write(data, reg, &val, 1);
}

/*
 * GT911 硬件复位 + 地址选择时序
 *
 * GT911 的 I2C 地址由复位释放瞬间 INT 引脚电平决定：
 *   INT 物理高 → 0x5D
 *   INT 物理低 → 0x14
 *
 * 复位流程（所有 API 使用 raw 版本，直接控制物理电平，绕开 active-low 逻辑翻转）：
 *   1. RST 拉低 → 芯片进入复位
 *   2. INT 设为输出并设目标电平 → 选择 I2C 地址
 *   3. RST 拉高 → 芯片退出复位，采样 INT 电平确定地址
 *   4. INT 切回输入 → 释放 INT 控制权，后续由 GT911 拉低触发中断
 *   5. 等待芯片初始化完成（约 100ms）
 *   6. 读 PID 寄存器确认芯片存在
 */
static int gt911_reset(struct gt911_data *data)
{
    struct device *dev = &data->client->dev;
    u8 pid[4];
    int ret;

    dev_info(dev, "GT911 RESET DESCRIPTOR VERSION\n");

    /* 1. RST 物理拉低，触发复位 */
    gpiod_direction_output_raw(data->reset_gpio, 0);
    msleep(20);

    /* 2. INT 物理低，选择 I2C 地址 */
    gpiod_direction_output_raw(data->irq_gpio, 0);
    msleep(5);

    dev_info(dev, "before release: rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

    /* 3. RST 物理拉高，释放复位 → 芯片采样 INT 确定地址 */
    gpiod_direction_output_raw(data->reset_gpio, 1);
    msleep(50);

    dev_info(dev, "after release:  rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

    /* 4. INT 切回输入模式，等待 GT911 初始化 */
    gpiod_direction_input(data->irq_gpio);
    msleep(100);

    dev_info(dev, "after int in:   rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

    /* 5. 读 PID 验证芯片是否正常响应 */
    ret = gt911_i2c_read(data, GT_PID_REG, pid, 4);
    if (ret < 0)
    {
        dev_err(dev, "failed to read product ID: %d\n", ret);
        return ret;
    }

    dev_info(dev, "GT911 ID: %c%c%c%c\n",
             pid[0], pid[1], pid[2], pid[3]);

    return 0;
}

/*
 * 向 Linux input 子系统注册多点触摸设备
 * - 支持 5 点触摸 (INPUT_MT_DIRECT)
 * - 同时注册 ABS_X/ABS_Y 兼容单点触摸应用
 * - ABS_MT_TOUCH_MAJOR 上报触摸面积
 */
static int gt911_input_register(struct gt911_data *data)
{
    struct device *dev = &data->client->dev;
    struct input_dev *idev;
    int ret;

    idev = devm_input_allocate_device(dev);
    if (!idev)
        return -ENOMEM;

    data->input_dev = idev;
    idev->name = "GT911 Touchscreen";
    idev->id.bustype = BUS_I2C;
    idev->dev.parent = dev;

    /* 多点触摸能力 */
    input_set_capability(idev, EV_KEY, BTN_TOUCH);
    input_set_abs_params(idev, ABS_MT_POSITION_X, 0, 1024, 0, 0);
    input_set_abs_params(idev, ABS_MT_POSITION_Y, 0, 600,  0, 0);
    input_set_abs_params(idev, ABS_MT_TOUCH_MAJOR, 0, 255,  0, 0);

    /* 单点兼容：input_mt_report_pointer_emulation 自动将 MT 事件转为 ABS_X/Y */
    input_set_abs_params(idev, ABS_X, 0, 1024, 0, 0);
    input_set_abs_params(idev, ABS_Y, 0, 600,  0, 0);

    /* 注册 5 个触摸槽位，自动释放未使用的槽位 */
    ret = input_mt_init_slots(idev, MAX_SUPPORT_POINTS,
                              INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
    if (ret)
    {
        dev_err(dev, "failed to init MT slots: %d\n", ret);
        return ret;
    }

    ret = input_register_device(idev);
    if (ret)
    {
        dev_err(dev, "failed to register input: %d\n", ret);
        return ret;
    }

    return 0;
}

/*
 * 触摸中断处理（threaded IRQ，在进程上下文中执行，可调用 I2C 休眠函数）
 *
 * 流程：
 *   1. 读状态寄存器 0x814E
 *   2. 检查 bit7 (Buffer Ready)，无数据则直接返回
 *   3. 从 bit3~0 获取触摸点数
 *   4. 从 0x814F 开始读取 N×8 字节触摸数据
 *   5. 逐点解析 (id, x, y, size) 并调用 input_mt_slot() 上报
 *   6. 调用 input_mt_sync_frame() + input_sync() 完成一帧
 *   7. 写 0x00 到状态寄存器 0x814E 清除 Buffer Ready，释放 INT
 *
 * 注意：状态清除必须写 0x814E (GT_GSTID_REG)，不能写 0x8040 (GT_CTRL_REG)，
 * 否则 GT911 不会释放 INT 引脚，后续中断无法再触发。
 */
static irqreturn_t gt911_irq_handler(int irq, void *dev_id)
{
    struct gt911_data *data = dev_id;
    u8 status;
    u8 touch_buf[MAX_SUPPORT_POINTS * GT911_TOUCH_POINT_SIZE];
    int num_touches;
    int ret;
    int i;

    /* 1. 读状态寄存器 */
    ret = gt911_i2c_read(data, GT_GSTID_REG, &status, 1);
    if (ret < 0)
        goto out;

    /* 2. bit7 = 0 表示 GT911 还没有准备好数据 */
    if (!(status & GT911_STATUS_BUF_READY))
        goto out;

    /* 3. 获取当前触摸点数 */
    num_touches = status & 0x0F;
    if (num_touches > MAX_SUPPORT_POINTS)
        num_touches = MAX_SUPPORT_POINTS;

    /* 4 & 5. 读取并上报每个触摸点 */
    if (num_touches > 0)
    {
        ret = gt911_i2c_read(data, GT_TP_REG, touch_buf,
                             num_touches * GT911_TOUCH_POINT_SIZE);
        if (ret < 0)
            goto clear_status;

        for (i = 0; i < num_touches; i++)
        {
            u8 *p = &touch_buf[i * GT911_TOUCH_POINT_SIZE];
            int id   = p[0] & 0x0F;           /* 触点 ID */
            int x    = p[1] | (p[2] << 8);   /* X 坐标 (LE) */
            int y    = p[3] | (p[4] << 8);   /* Y 坐标 (LE) */
            int size = p[5] | (p[6] << 8);    /* 触摸面积 (LE) */

            if (id >= MAX_SUPPORT_POINTS)
                continue;

            input_mt_slot(data->input_dev, id);
            input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
            input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, size);
        }
    }

    /* 6. 帧同步 → 释放不活跃触点，模拟单点事件 */
    input_mt_sync_frame(data->input_dev);
    input_mt_report_pointer_emulation(data->input_dev, true);
    input_sync(data->input_dev);

clear_status:
    /* 7. 清除状态 → GT911 释放 INT 引脚为高，准备下一轮中断 */
    gt911_i2c_write_byte(data, GT_GSTID_REG, 0x00);

out:
    return IRQ_HANDLED;
}

static int gt911_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct device *dev = &client->dev;
    struct gt911_data *data;
    int ret;

    /* 分配私有数据，devm_ 生命周期跟随设备，remove 时自动释放 */
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);  /* 挂到 I2C client 上，remove/中断中可取回 */

    /*
     * 从设备树获取 GPIO
     * devm_gpiod_get_optional(dev, "reset", ...) 自动匹配设备树属性 "reset-gpios"
     * 同理 "irq" 匹配 "irq-gpios"
     */
    data->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(data->reset_gpio))
    {
        dev_err(dev, "failed to get reset gpio: %ld\n", PTR_ERR(data->reset_gpio));
        return PTR_ERR(data->reset_gpio);
    }
    if (!data->reset_gpio)
    {
        dev_err(dev, "missing reset-gpios\n");
        return -EINVAL;
    }
    dev_info(dev, "reset gpio descriptor OK\n");

    data->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
    if (IS_ERR(data->irq_gpio))
    {
        dev_err(dev, "failed to get irq gpio: %ld\n", PTR_ERR(data->irq_gpio));
        return PTR_ERR(data->irq_gpio);
    }
    if (!data->irq_gpio)
    {
        dev_err(dev, "missing irq-gpios\n");
        return -EINVAL;
    }
    dev_info(dev, "irq gpio descriptor OK\n");

    /* 硬件复位 + PID 验证 */
    ret = gt911_reset(data);
    if (ret)
    {
        dev_err(dev, "failed to reset GT911: %d\n", ret);
        return ret;
    }

    /* 注册 input 设备 → 生成 /dev/input/eventX */
    ret = gt911_input_register(data);
    if (ret)
        return ret;

    /*
     * 中断申请：
     * - gpiod_to_irq 将 GPIO descriptor 转为 IRQ 号
     * - IRQF_TRIGGER_FALLING: GT911 有触摸时拉低 INT
     * - IRQF_ONESHOT: 中断处理完成前保持屏蔽，防止重入
     * - threaded IRQ (handler=NULL): 整个处理在进程上下文中运行，可休眠
     */
    data->irq = gpiod_to_irq(data->irq_gpio);
    if (data->irq < 0)
        return data->irq;

    ret = devm_request_threaded_irq(dev, data->irq,
                                    NULL, gt911_irq_handler,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                    "gt911_ts", data);
    if (ret)
    {
        dev_err(dev, "failed to request irq: %d\n", ret);
        return ret;
    }

    dev_info(dev, "GT911 probed\n");
    return 0;
}

/* 卸载时拉低 RST 使芯片进入复位，降低功耗 */
static int gt911_remove(struct i2c_client *client)
{
    struct gt911_data *data = i2c_get_clientdata(client);

    gpiod_set_value_cansleep(data->reset_gpio, 1); /* assert reset = 物理低 */
    dev_info(&client->dev, "GT911 removed\n");
    return 0;
}

static const struct i2c_device_id gt911_id[] = {
    {"gt911", 0},
    {},
};

static const struct of_device_id gt911_of_match[] = {
    {.compatible = "sensor_collect,gt911"},  /* 匹配设备树中的 compatible 属性 */
    {},
};

MODULE_DEVICE_TABLE(of, gt911_of_match);  /* 导出给 udev/modprobe，支持设备树自动加载 */

static struct i2c_driver gt911_driver = {
    .probe      = gt911_probe,
    .remove     = gt911_remove,
    .id_table   = gt911_id,
    .driver     = {
        .name           = "gt911",
        .owner          = THIS_MODULE,
        .of_match_table = gt911_of_match,
    },
};

/* module_i2c_driver 宏等价于下面的 init/exit，但展开来写能更清楚地看到注册过程 */
static int __init gt911_init(void)
{
    return i2c_add_driver(&gt911_driver);
}

static void __exit gt911_exit(void)
{
    i2c_del_driver(&gt911_driver);
}

module_init(gt911_init);
module_exit(gt911_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Farewellove");
