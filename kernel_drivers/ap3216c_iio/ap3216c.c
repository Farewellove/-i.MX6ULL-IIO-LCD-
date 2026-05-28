/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/9 20:58:22
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/13 22:07:08
 * @Description: AP3216C 光传感器 IIO 驱动
 *               I2C1, 地址 0x1E, 支持 ALS(环境光)/PS(接近)/IR(红外) 三通道
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
    struct mutex lock;       /* 保护 I2C 读写不被并发打断 */
};

/* AP3216C 的寄存器是 8 位地址，可以直接用 smbus API */
static int ap3216c_read_reg(struct ap3216c_data *data, u8 reg)
{
    return i2c_smbus_read_byte_data(data->client, reg);
}

static int ap3216c_write_reg(struct ap3216c_data *data, u8 reg, u8 val)
{
    return i2c_smbus_write_byte_data(data->client, reg, val);
}

/*
 * 硬件初始化：先软件复位，再开启全部三个传感器通道
 */
static int ap3216c_hw_init(struct ap3216c_data *data)
{
    int ret;

    /* 0x04：软件复位 */
    ret = ap3216c_write_reg(data, AP3216C_SYSTEMCONG, 0x04);
    if (ret < 0)
        return ret;

    msleep(50);

    /* 0x03：开启 ALS + PS + IR */
    ret = ap3216c_write_reg(data, AP3216C_SYSTEMCONG, 0x03);
    if (ret < 0)
        return ret;

    msleep(50);

    return 0;
}

/* 读取环境光强度 (ALS)，16 位数据：high << 8 | low */
static int ap3216c_read_als(struct ap3216c_data *data, int *val)
{
    int low, high;

    low  = ap3216c_read_reg(data, AP3216C_ALSDATALOW);
    high = ap3216c_read_reg(data, AP3216C_ALSDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    *val = (high << 8) | low;
    return 0;
}

/* 读取红外强度 (IR)，取 high 低 2 位 + low 8 位 */
static int ap3216c_read_ir(struct ap3216c_data *data, int *val)
{
    int low, high;

    low  = ap3216c_read_reg(data, AP3216C_IRDATALOW);
    high = ap3216c_read_reg(data, AP3216C_IRDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    *val = ((high & 0x03) << 8) | (low & 0xFF);

    return 0;
}

/* 读取接近传感器 (PS)，取 high 低 6 位 + low 低 4 位 */
static int ap3216c_read_ps(struct ap3216c_data *data, int *val)
{
    int low, high;

    low  = ap3216c_read_reg(data, AP3216C_PSDATALOW);
    high = ap3216c_read_reg(data, AP3216C_PSDATAHIGH);

    if (low < 0 || high < 0)
        return -EIO;

    *val = ((high & 0x3F) << 4) | (low & 0x0F);

    return 0;
}

/* IIO 通道定义：光照、接近、红外 */
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

/* IIO read_raw 回调：用户空间读 sysfs 时触发 */
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
        /* 原始值不做缩放，scale = 1 */
        mutex_lock(&data->lock);
        *val = 1;
        *val2 = 0;
        mutex_unlock(&data->lock);
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

/*
 * iio_info 必须定义为 static const 全局对象，
 * 不能是 probe 里的临时变量，否则内核可能访问已释放内存。
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

    /* iio_device_alloc 同时分配 IIO 设备 + 私有数据 */
    indio_dev = iio_device_alloc(sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);      /* 取出嵌入在 IIO 设备后的私有数据指针 */
    data->client = client;
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
    indio_dev->modes = INDIO_DIRECT_MODE;  /* 按需读取，不连续采样 */

    ret = iio_device_register(indio_dev);
    if (ret)
        goto err_free_iio;

    i2c_set_clientdata(client, indio_dev);
    printk("ap3216c iio probe\r\n");
    return 0;

err_free_iio:
    iio_device_free(indio_dev);
    return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
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

/*
 * MODULE_DEVICE_TABLE 导出设备树匹配表到模块 .modinfo 段。
 * 没有它：驱动加载了但匹配不到设备树节点 → probe 不会被调用。
 */
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static struct i2c_driver ap3216c_driver = {
    .probe  = ap3216c_probe,
    .remove = ap3216c_remove,
    .driver = {
        .name           = "ap3216c",
        .owner          = THIS_MODULE,
        .of_match_table = ap3216c_of_match,
    },
    .id_table = ap3216c_id,
};

static int __init ap3216c_init(void)
{
    return i2c_add_driver(&ap3216c_driver);
}

static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FAREWELLOVE");
