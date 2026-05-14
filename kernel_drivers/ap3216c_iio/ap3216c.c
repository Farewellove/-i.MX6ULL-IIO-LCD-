/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/9 20:58:22
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/13 22:07:08
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ap3216creg.h"

#define AP3216C_CNT 1
#define AP3216C_NAME "ap3216c"

struct ap3216c_data
{
    struct i2c_client *client;
    struct mutex lock;
};

static int ap3216c_read_reg(struct ap3216c_data *data, u8 reg)
{
    return i2c_smbus_read_byte_data(data->client, reg);
}

/* 写一个寄存器 */
static int ap3216c_write_reg(struct ap3216c_data *data, u8 reg, u8 val)
{
    return i2c_smbus_write_byte_data(data->client, reg, val);
}

/* 初始化 AP3216C */
static int ap3216c_hw_init(struct ap3216c_data *data)
{
    int ret;

    /*
     * 0x04：软件复位
     */
    ret = ap3216c_write_reg(data, AP3216C_SYSTEMCONG, 0x04);
    if (ret < 0)
        return ret;

    msleep(50);

    /*
     * 0x03：开启 ALS + PS + IR
     * 虽然当前第一版只读 ALS，但先整体打开没有问题
     */
    ret = ap3216c_write_reg(data, AP3216C_SYSTEMCONG, 0x03);
    if (ret < 0)
        return ret;

    msleep(50);

    return 0;
}

static int ap3216c_read_als(struct ap3216c_data *data, int *val)
{
    int low, high;

    low = ap3216c_read_reg(data, AP3216C_ALSDATALOW);
    high = ap3216c_read_reg(data, AP3216C_ALSDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    *val = (high << 8) | low; // 拼接数据，high左移8位然后和low拼接在一起
    return 0;
}

static int ap3216c_read_ir(struct ap3216c_data *data, int *val)
{
    int low, high;

    low = ap3216c_read_reg(data, AP3216C_IRDATALOW);
    high = ap3216c_read_reg(data, AP3216C_IRDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    /*
     * IR 数据一般不是完整 16 位。
     * 这里取 high 低 2 位 + low 8 位。
     */
    *val = ((high & 0x03) << 8) | (low & 0xFF);

    return 0;
}

static int ap3216c_read_ps(struct ap3216c_data *data, int *val)
{
    int low, high;

    low = ap3216c_read_reg(data, AP3216C_PSDATALOW);
    high = ap3216c_read_reg(data, AP3216C_PSDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    /*
     * PS 常见拼接方式：
     * high 低 6 位作为高位，low 低 4 位作为低位。
     */
    *val = ((high & 0x3F) << 4) | (low & 0x0F);

    return 0;
}

static const struct iio_chan_spec ap3216c_channels[] = {
    {
        .type = IIO_LIGHT,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_PROXIMITY,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_INTENSITY,
        .modified = 1,
        .channel2 = IIO_MOD_LIGHT_IR,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
    },
};

static int ap3216c_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct ap3216c_data *data = iio_priv(indio_dev);
    int ret;

    switch (mask)
    {
    case IIO_CHAN_INFO_RAW:
        mutex_lock(&data->lock);

        switch (chan->type)
        {
        case IIO_LIGHT:
            ret = ap3216c_read_als(data, val);
            break;

        case IIO_PROXIMITY:
            ret = ap3216c_read_ps(data, val);
            break;

        case IIO_INTENSITY:
            ret = ap3216c_read_ir(data, val);
            break;

        default:

            ret = -EINVAL;
            break;
        }

        mutex_unlock(&data->lock);

        if (ret < 0)
            return ret;

        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        mutex_lock(&data->lock);
        switch (chan->type)
        {
        case IIO_LIGHT:
            *val = 1;
            *val2 = 0;
            break;
        case IIO_PROXIMITY:
            *val = 1;
            *val2 = 0;
            break;
        case IIO_INTENSITY:
            *val = 1;
            *val2 = 0;
            break;
        default:

            return -EINVAL;
        }
        mutex_unlock(&data->lock);
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

/*
 * 注意：
 * 这个必须定义成 static const 全局对象，
 * 不要写成 probe 里的临时结构体。
 */
static const struct iio_info ap3216c_info = {
    .read_raw = ap3216c_read_raw,
};

static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;
    struct iio_dev *indio_dev;
    struct ap3216c_data *data;

    indio_dev = iio_device_alloc(sizeof(*data));
    if (!indio_dev)
    {
        return -ENOMEM;
    }

    data = iio_priv(indio_dev); // 取出私有数据
    data->client = client;      // 绑定client

    mutex_init(&data->lock);

    ret = ap3216c_hw_init(data);
    if (ret)
    {
        dev_err(&client->dev, "failed to init ap3216c\n");
        goto err_free_iio;
    }

    indio_dev->name = AP3216C_NAME;
    indio_dev->dev.parent = &client->dev;
    indio_dev->info = &ap3216c_info;
    indio_dev->channels = ap3216c_channels;
    indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);
    indio_dev->modes = INDIO_DIRECT_MODE; // 直接读取模式

    ret = iio_device_register(indio_dev); // 注册iio设备
    if (ret)
        goto err_free_iio;

    i2c_set_clientdata(client, indio_dev); // 把 IIO 设备挂到 I2C client 上
    printk("ap3216c iio probe\r\n");
    return 0;

err_free_iio:
    iio_device_free(indio_dev);
    return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
    // 从I2C client中获取设备结构体
    struct iio_dev *indio_dev = i2c_get_clientdata(client);

    if (indio_dev)
    {
        iio_device_unregister(indio_dev);
        iio_device_free(indio_dev);
    }

    printk("ap3216c iio remove\n");

    return 0;
}

static const struct i2c_device_id ap3216c_id[] = {
    {"ap3216c", 0},
    {},
};

static struct of_device_id ap3216c_of_match[] = {
    {.compatible = "sensor_collect,ap3216c"},
    {},
};

/*这行作用：告诉内核，你的驱动支持哪些设备树匹配
不加：内核根本不知道你的驱动能匹配 sensor_collect,ap3216c
结果：驱动加载了 → 不匹配 → probe 不进*/
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static struct i2c_driver ap3216c_driver = {
    .probe = ap3216c_probe,
    .remove = ap3216c_remove,
    .driver = {
        .name = "ap3216c",
        .owner = THIS_MODULE,
        .of_match_table = ap3216c_of_match,
    },
    .id_table = ap3216c_id,
};

static int __init ap3216c_init(void)
{
    int ret = 0;
    ret = i2c_add_driver(&ap3216c_driver);
    return ret;
}

static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FAREWELLOVE");