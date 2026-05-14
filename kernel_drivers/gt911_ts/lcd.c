/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/12 22:00:29
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/14 22:35:53
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/i2c.h>
#include <asm/unaligned.h>

#define GT_CTRL_REG 0x8040 /* GT911 控制寄存器         */
#define GT_PID_REG 0x8140  /* GT911 产品ID寄存器       */

#define GT_GSTID_REG 0x814E  /* GT911 当前检测到的触摸情况 */
#define GT_TP1_REG 0x814F    /* 第一个触摸点数据地址 */
#define GT_TP2_REG 0x8157    /* 第二个触摸点数据地址 */
#define GT_TP3_REG 0x815F    /* 第三个触摸点数据地址 */
#define GT_TP4_REG 0x8167    /* 第四个触摸点数据地址  */
#define GT_TP5_REG 0x816F    /* 第五个触摸点数据地址   */
#define MAX_SUPPORT_POINTS 5 /* 最多5点电容触摸 */

struct gt911_dev
{
    int irq_pin, reset_pin;    /* 中断和复位IO      */
    int irqnum;                /* 中断号            */
    int irqtype;               /* 中断类型           */
    int max_x;                 /* 最大横坐标         */
    int max_y;                 /* 最大纵坐标         */
    void *private_data;        /* 私有数据           */
    struct input_dev *input;   /* input结构体        */
    struct i2c_client *client; /* I2C客户端          */
};

static struct gt911_dev gt911;

const u8 irq_table[] = {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_LOW, IRQ_TYPE_LEVEL_HIGH}; /* 触发方式 */

/*
 * @description     : 复位GT911
 * @param - client  : 要操作的i2c
 * @param - dev     : 自定义的touch设备
 * @return          : 0，成功;其他负值,失败
 */
static int gt911_ts_reset(struct i2c_client *client, struct gt911_dev *dev)
{
    int ret = 0;

    /* 申请复位IO */
    if (gpio_is_valid(dev->reset_pin))
    {
        /* 申请复位IO，并且默认输出高电平 */
        ret = devm_gpio_request_one(&client->dev,
                                    dev->reset_pin, GPIOF_OUT_INIT_HIGH,
                                    "gt911 reset");
        if (ret)
        {
            return ret;
        }
    }

    /* 申请中断IO */
    if (gpio_is_valid(dev->irq_pin))
    {
        /* 申请中断IO，并且默认输出高电平 */
        ret = devm_gpio_request_one(&client->dev,
                                    dev->irq_pin, GPIOF_OUT_INIT_HIGH,
                                    "gt911 int");
        if (ret)
        {
            return ret;
        }
    }

    /* 初始化GT911，要严格按照GT911时序要求 */
    gpio_set_value(dev->reset_pin, 0); /* 复位GT911 */
    msleep(10);
    gpio_set_value(dev->reset_pin, 1); /* 停止复位GT911 */
    msleep(10);
    gpio_set_value(dev->irq_pin, 0); /* 拉低INT引脚 */
    msleep(50);
    gpio_direction_input(dev->irq_pin); /* INT引脚设置为输入 */

    return 0;
}

/*
 * @description : 从GT911读取多个寄存器数据
 * @param - dev:  GT911设备
 * @param - reg:  要读取的寄存器首地址
 * @param - buf:  读取到的数据
 * @param - len:  要读取的数据长度
 * @return       : 操作结果
 */
static int gt911_read_regs(struct gt911_dev *dev, u16 reg, u8 *buf, int len)
{
    int ret;
    u8 regdata[2];
    struct i2c_msg msg[2];
    struct i2c_client *client = (struct i2c_client *)dev->client;

    /* GT911寄存器长度为2个字节 */
    regdata[0] = reg >> 8;
    regdata[1] = reg & 0xFF;

    /* msg[0]为发送要读取的首地址 */
    msg[0].addr = client->addr; /* gt911地址 */
    msg[0].flags = !I2C_M_RD;   /* 标记为发送数据 */
    msg[0].buf = &regdata[0];   /* 读取的首地址 */
    msg[0].len = 2;             /* reg长度*/

    /* msg[1]读取数据 */
    msg[1].addr = client->addr; /* gt911地址 */
    msg[1].flags = I2C_M_RD;    /* 标记为读取数据*/
    msg[1].buf = buf;           /* 读取数据缓冲区 */
    msg[1].len = len;           /* 要读取的数据长度*/

    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret == 2)
    {
        ret = 0;
    }
    else
    {
        ret = -EREMOTEIO;
    }
    return ret;
}

/*
 * @description : 向GT911多个寄存器写入数据
 * @param - dev:  GT911设备
 * @param - reg:  要写入的寄存器首地址
 * @param - buf:  要写入的数据缓冲区
 * @param - len:  要写入的数据长度
 * @return      : 操作结果
 */
static s32 gt911_write_regs(struct gt911_dev *dev, u16 reg, u8 *buf, u8 len)
{
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client *client = (struct i2c_client *)dev->client;

    b[0] = reg >> 8;         /* 寄存器首地址高8位 */
    b[1] = reg & 0xFF;       /* 寄存器首地址低8位 */
    memcpy(&b[2], buf, len); /* 将要写入的数据拷贝到数组b里面 */

    msg.addr = client->addr; /* gt911地址 */
    msg.flags = 0;           /* 标记为写数据 */

    msg.buf = b;       /* 要写入的数据缓冲区 */
    msg.len = len + 2; /* 要写入的数据长度 */

    return i2c_transfer(client->adapter, &msg, 1);
}

static irqreturn_t gt911_irq_handler(int irq, void *dev_id)
{
    int touch_num = 0;
    int input_x, input_y;
    int id = 0;
    int ret = 0;
    u8 data;
    u8 touch_data[5];
    struct gt911_dev *dev = dev_id;

    ret = gt911_read_regs(dev, GT_GSTID_REG, &data, 1);
    if (data == 0x00)
    { /* 没有触摸数据，直接返回 */
        goto fail;
    }
    else
    { /* 统计触摸点数据 */
        touch_num = data & 0x0f;
    }

    /* 由于GT911没有硬件检测每个触摸点按下和抬起，因此每个触摸点的抬起和按
     * 下不好处理，尝试过一些方法，但是效果都不好，因此这里暂时使用单点触摸
     */
    if (touch_num)
    { /* 单点触摸按下 */
        gt911_read_regs(dev, GT_TP1_REG, touch_data, 5);
        id = touch_data[0] & 0x0F;
        if (id == 0)
        {
            input_x = touch_data[1] | (touch_data[2] << 8);
            input_y = touch_data[3] | (touch_data[4] << 8);

            input_mt_slot(dev->input, id);
            input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, true);
            input_report_abs(dev->input, ABS_MT_POSITION_X, input_x);
            input_report_abs(dev->input, ABS_MT_POSITION_Y, input_y);
        }
    }
    else if (touch_num == 0)
    { /* 单点触摸释放 */
        input_mt_slot(dev->input, id);
        input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, false);
    }

    input_mt_report_pointer_emulation(dev->input, true);
    input_sync(dev->input);

    data = 0x00; /* 向0x814E寄存器写0 */
    gt911_write_regs(dev, GT_GSTID_REG, &data, 1);

fail:
    return IRQ_HANDLED;
}

/*
 * @description     : GT911中断初始化
 * @param - client  : 要操作的i2c
 * @param - dev     : 自定义的touch设备
 * @return          : 0，成功;其他负值,失败
 */
static int gt911_ts_irq(struct i2c_client *client, struct gt911_dev *dev)
{
    int ret = 0;
    /* 申请中断,client->irq就是IO中断 */
    ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
                                    gt911_irq_handler, irq_table[dev->irqtype] | IRQF_ONESHOT,
                                    client->name, &gt911);
    if (ret)
    {
        dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
        return ret;
    }

    return 0;
}

/*
 * @description     : GT911读取固件配置并初始化
 * @param - client  : 要操作的i2c
 * @param - dev     : 自定义的touch设备
 * @return          : 0，成功;其他负值,失败
 */
static int gt911_read_firmware(struct i2c_client *client, struct gt911_dev *dev)
{
    int ret = 0, version = 0;
    u16 id = 0;
    u8 data[6] = {0};
    char id_str[5];

    /* 读取 PID (0x8140, 6字节: 4字节ID + 2字节版本) */
    ret = gt911_read_regs(dev, GT_PID_REG, data, 6);
    if (ret)
    {
        dev_err(&client->dev, "Unable to read PID.\n");
        return ret;
    }
    memcpy(id_str, data, 4);
    id_str[4] = 0;
    if (kstrtou16(id_str, 10, &id))
        id = 0x1001;
    version = get_unaligned_le16(&data[4]);
    dev_info(&client->dev, "ID %d, version: %04x\n", id, version);

    /* 使用默认配置 800x480，下降沿触发 */
    dev->max_x = 800;
    dev->max_y = 480;
    dev->irqtype = 1; /* IRQ_TYPE_EDGE_FALLING */

    /*
     * GT911 完整配置表 (184字节)，从 0x8047 开始写入。
     * 这是针对 800x480 屏幕的标准配置，下降沿触发。
     * 配置表末尾包含校验和，芯片会验证。
     */
    {
        u8 cfg_800x480[] = {
            0x00, 0x20, 0x03, 0x00, 0x05, 0x0A, 0x0D, 0x00,
            0x08, 0x28, 0x1E, 0x50, 0x32, 0x03, 0x05, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x87, 0x28, 0x0A, 0x1C,
            0x1D, 0x31, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        gt911_write_regs(dev, 0x8047, cfg_800x480, sizeof(cfg_800x480));
    }
    /* 写 0x01 到 0x8040 触发配置更新 + 校验 */
    {
        u8 val = 0x01;
        gt911_write_regs(dev, GT_CTRL_REG, &val, 1);
    }
    msleep(200);

    dev_info(&client->dev, "Using config: X_MAX=%d Y_MAX=%d TRIGGER=%d\n",
             dev->max_x, dev->max_y, dev->irqtype);

    return 0;
}

static int gt911_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    u8 cmd;

    /* 清零全局设备结构体，确保干净状态 */
    memset(&gt911, 0, sizeof(gt911));
    gt911.client = client;

    /* 1，获取设备树中的中断和复位引脚 */
    gt911.irq_pin = of_get_named_gpio(client->dev.of_node, "irq-gpios", 0);
    if (gt911.irq_pin < 0)
        gt911.irq_pin = of_get_named_gpio(client->dev.of_node, "interrupt-gpios", 0);
    gt911.reset_pin = of_get_named_gpio(client->dev.of_node, "reset-gpios", 0);

    /* 2，硬件复位GT911 */
    ret = gt911_ts_reset(client, &gt911);
    if (ret < 0)
    {
        goto fail;
    }

    /* 3，软件复位: 写0x02到0x8040 -> 延时100ms -> 写0x00到0x8040 -> 延时100ms
     * 软件复位后配置表才会从Flash加载到0x8047区域
     */
    cmd = 0x02;
    gt911_write_regs(&gt911, GT_CTRL_REG, &cmd, 1);
    msleep(100);
    cmd = 0x00;
    gt911_write_regs(&gt911, GT_CTRL_REG, &cmd, 1);
    msleep(100);

    /* 4,读取固件配置 */
    ret = gt911_read_firmware(client, &gt911);
    if (ret != 0)
    {
        printk("Fail !!! check !!\r\n");
        goto fail;
    }

    /* 调试: 读取 INT 引脚电平 */
    if (gpio_is_valid(gt911.irq_pin))
    {
        int level = gpio_get_value(gt911.irq_pin);
        dev_info(&client->dev, "INT pin level after reset: %d (0=low,1=high)\n", level);
    }

    /* 5，input设备注册 */
    gt911.input = input_allocate_device();
    if (!gt911.input)
    {
        ret = -ENOMEM;
        goto fail;
    }
    gt911.input->name = client->name;
    gt911.input->id.bustype = BUS_I2C;
    gt911.input->dev.parent = &client->dev;

    __set_bit(EV_KEY, gt911.input->evbit);
    __set_bit(EV_ABS, gt911.input->evbit);
    __set_bit(BTN_TOUCH, gt911.input->keybit);

    input_set_abs_params(gt911.input, ABS_X, 0, gt911.max_x, 0, 0);
    input_set_abs_params(gt911.input, ABS_Y, 0, gt911.max_y, 0, 0);
    input_set_abs_params(gt911.input, ABS_MT_POSITION_X, 0, gt911.max_x, 0, 0);
    input_set_abs_params(gt911.input, ABS_MT_POSITION_Y, 0, gt911.max_y, 0, 0);
    ret = input_mt_init_slots(gt911.input, MAX_SUPPORT_POINTS, 0);
    if (ret)
    {
        goto fail;
    }

    ret = input_register_device(gt911.input);
    if (ret)
        goto fail;

    /* 6，最后初始化中断 */
    ret = gt911_ts_irq(client, &gt911);
    if (ret < 0)
    {
        goto fail;
    }
    return 0;

fail:
    return ret;
}

/*
 * @description     : i2c驱动的remove函数，移除i2c驱动的时候此函数会执行
 * @param - client  : i2c设备
 * @return          : 0，成功;其他负值,失败
 */
static int gt911_remove(struct i2c_client *client)
{
    if (gt911.input)
    {
        input_unregister_device(gt911.input);
        gt911.input = NULL;
    }
    /* 清零全局变量，确保下次 probe 时状态干净 */
    memset(&gt911, 0, sizeof(gt911));
    return 0;
}

/*
 *  传统驱动匹配表
 */
static const struct i2c_device_id gt911_id_table[] = {
    {"sensor_collect,gt911", 0},
    {/* sentinel */}};

/*
 * 设备树匹配表
 */
static const struct of_device_id gt911_of_match[] = {
    {.compatible = "sensor_collect,gt911"},
    {/* sentinel */}};

MODULE_DEVICE_TABLE(i2c, gt911_id_table);

/* i2c驱动结构体 */
static struct i2c_driver gt911_i2c_driver = {
    .driver = {
        .name = "gt911",
        .owner = THIS_MODULE,
        .of_match_table = gt911_of_match,
    },
    .id_table = gt911_id_table,
    .probe = gt911_probe,
    .remove = gt911_remove,
};

module_i2c_driver(gt911_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Farewellove");