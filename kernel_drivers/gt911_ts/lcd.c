/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/12 22:00:29
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/27 23:04:05
 * @Description:
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

#define GT_CTRL_REG 0X8040  /* GT9147控制寄存器         */
#define GT_MODSW_REG 0X804D /* GT9147模式切换寄存器        */
#define GT_CFGS_REG 0X8047  /* GT9147配置起始地址寄存器    */
#define GT_CHECK_REG 0X80FF /* GT9147校验和寄存器       */
#define GT_PID_REG 0X8140   /* GT9147产品ID寄存器       */

#define GT_GSTID_REG 0X814E  /* GT9147当前检测到的触摸情况 */
#define GT_TP_REG 0X814F     /* 第一个触摸点数据地址，后续点连续排列 */
#define MAX_SUPPORT_POINTS 5 /* 最多5点电容触摸 */

#define GT911_STATUS_BUF_READY 0x80
#define GT911_TOUCH_POINT_SIZE 8

struct gt911_data
{
    struct i2c_client *client;
    struct gpio_desc *reset_gpio;
    struct gpio_desc *irq_gpio;
    int irq;                     // 中断号
    struct input_dev *input_dev; /* input 设备 */
};

static int gt911_i2c_read(struct gt911_data *data, u16 reg, u8 *buf, int len)
{
    struct i2c_client *client = data->client;
    u8 reg_buf[2];
    struct i2c_msg msgs[2];
    int ret;

    // 高低位分布
    reg_buf[0] = (reg >> 8) & 0xFF;
    reg_buf[1] = reg & 0xFF;

    // 第一条消息：向 I2C 设备写入 16 位寄存器地址
    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = reg_buf;

    // 第二条信息读取数据
    msgs[1].addr = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = len;
    msgs[1].buf = buf;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret == 2)
        return 0;
    else if (ret < 0)
        return ret;
    else
        return -EIO;
}

static int gt911_i2c_write(struct gt911_data *data, u16 reg, u8 *buf, int len)
{
    struct i2c_client *client = data->client;
    u8 *tx_buf;
    struct i2c_msg msg;
    int ret;

    tx_buf = kmalloc(2 + len, GFP_KERNEL);
    // 写操作需要将i2c寄存器地址一并发送，由于gt911寄存器是16位，所以比len多2个字节
    if (!tx_buf)
    {
        return -ENOMEM;
    }

    tx_buf[0] = (reg >> 8) & 0xFF;
    tx_buf[1] = reg & 0xFF;
    memcpy(&tx_buf[2], buf, len);

    msg.addr = client->addr;
    msg.flags = 0;
    msg.buf = tx_buf;
    msg.len = 2 + len;

    ret = i2c_transfer(client->adapter, &msg, 1);
    kfree(tx_buf);

    if (ret == 1)
        return 0;
    else if (ret < 0)
        return ret;
    else
        return -EIO;
}

static inline int gt911_i2c_write_byte(struct gt911_data *data, u16 reg, u8 val)
{
    return gt911_i2c_write(data, reg, &val, 1);
}

static int gt911_reset(struct gt911_data *data)
{
    struct device *dev = &data->client->dev;
    u8 pid[4];
    int ret;

    dev_info(dev, "GT911 RESET DESCRIPTOR VERSION\n");

    gpiod_direction_output_raw(data->reset_gpio, 0);
    msleep(20);

    gpiod_direction_output_raw(data->irq_gpio, 0);
    msleep(5);

    dev_info(dev, "before release: rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

    gpiod_direction_output_raw(data->reset_gpio, 1);
    msleep(50);

    dev_info(dev, "after release:  rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

    gpiod_direction_input(data->irq_gpio);
    msleep(100);

    dev_info(dev, "after int in:   rst raw=%d int raw=%d\n",
             gpiod_get_raw_value(data->reset_gpio),
             gpiod_get_raw_value(data->irq_gpio));

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

    input_set_capability(idev, EV_KEY, BTN_TOUCH);
    input_set_abs_params(idev, ABS_MT_POSITION_X, 0, 1024, 0, 0);
    input_set_abs_params(idev, ABS_MT_POSITION_Y, 0, 600, 0, 0);
    input_set_abs_params(idev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(idev, ABS_X, 0, 1024, 0, 0);
    input_set_abs_params(idev, ABS_Y, 0, 600, 0, 0);

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

static irqreturn_t gt911_irq_handler(int irq, void *dev_id)
{
    struct gt911_data *data = dev_id;
    u8 status;
    u8 touch_buf[MAX_SUPPORT_POINTS * GT911_TOUCH_POINT_SIZE];
    int num_touches;
    int ret;
    int i;

    ret = gt911_i2c_read(data, GT_GSTID_REG, &status, 1);
    if (ret < 0)
        goto out;

    if (!(status & GT911_STATUS_BUF_READY))
        goto out;

    num_touches = status & 0x0F;
    if (num_touches > MAX_SUPPORT_POINTS)
        num_touches = MAX_SUPPORT_POINTS;

    if (num_touches > 0)
    {
        ret = gt911_i2c_read(data, GT_TP_REG, touch_buf,
                             num_touches * GT911_TOUCH_POINT_SIZE);
        if (ret < 0)
            goto clear_status;

        for (i = 0; i < num_touches; i++)
        {
            u8 *p = &touch_buf[i * GT911_TOUCH_POINT_SIZE];
            int id = p[0] & 0x0F;
            int x = p[1] | (p[2] << 8);
            int y = p[3] | (p[4] << 8);
            int size = p[5] | (p[6] << 8);

            if (id >= MAX_SUPPORT_POINTS)
                continue;

            input_mt_slot(data->input_dev, id);
            input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
            input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, size);
        }
    }

    input_mt_sync_frame(data->input_dev);
    input_mt_report_pointer_emulation(data->input_dev, true);
    input_sync(data->input_dev);

clear_status:
    gt911_i2c_write_byte(data, GT_GSTID_REG, 0x00);

out:
    return IRQ_HANDLED;
}

static int gt911_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct device *dev = &client->dev;
    struct gt911_data *data;
    int ret;
    // 分配私有数据内存，devm可以自己删除分配内存
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
    {
        return -ENOMEM;
    }

    data->client = client;
    i2c_set_clientdata(client, data);

    /* 2. 从设备树获取 RST GPIO */
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

    /* 3. 从设备树获取 INT GPIO */
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

    ret = gt911_reset(data);
    if (ret)
    {
        dev_err(dev, "failed to reset GT911: %d\n", ret);
        return ret;
    }

    ret = gt911_input_register(data);
    if (ret)
        return ret;

    /* 申请中断 */
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

static int gt911_remove(struct i2c_client *client)
{
    struct gt911_data *data = i2c_get_clientdata(client);

    gpiod_set_value_cansleep(data->reset_gpio, 1); /* assert reset */
    dev_info(&client->dev, "GT911 removed\n");
    return 0;
}

static const struct i2c_device_id gt911_id[] = {
    {"gt911", 0},
    {},
};

static const struct of_device_id gt911_of_match[] = {
    {.compatible = "sensor_collect,gt911"},
    {},
};

MODULE_DEVICE_TABLE(of, gt911_of_match);

static struct i2c_driver gt911_driver = {
    .probe = gt911_probe,
    .remove = gt911_remove,
    .id_table = gt911_id,
    .driver = {
        .name = "gt911",
        .owner = THIS_MODULE,
        .of_match_table = gt911_of_match,
    },
};

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