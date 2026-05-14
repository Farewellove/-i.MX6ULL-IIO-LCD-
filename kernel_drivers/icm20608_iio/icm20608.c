/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/21 15:02:44
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/13 22:06:58
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include "icm20608reg.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

static struct icm20608_dev
{

    struct spi_device *spi;
    struct mutex lock; // SPI 访问锁
};

static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg)
{
    u8 rx, tx;
    int ret;
    tx = reg | ICM20608_REG_READ;
    ret = spi_write_then_read(dev->spi, &tx, 1, &rx, 1);
    if (ret < 0)
    {
        return ret;
    }
    return rx;
}

// SPI 写寄存器
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 val)
{
    u8 tx[2];
    tx[0] = reg & ICM20608_REG_WRITE; // 写操作
    tx[1] = val;
    return spi_write(dev->spi, tx, 2);
}

static const struct iio_chan_spec icm20608_channels[] = {
    {.type = IIO_ACCEL, .indexed = 1, .channel = 0, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_ACCEL, .indexed = 1, .channel = 1, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_ACCEL, .indexed = 1, .channel = 2, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_ANGL_VEL, .indexed = 1, .channel = 0, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_ANGL_VEL, .indexed = 1, .channel = 1, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_ANGL_VEL, .indexed = 1, .channel = 2, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
    {.type = IIO_TEMP, .indexed = 1, .channel = 0, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW)},
};

// 六轴 + 温度读取函数
static int read_accel_x(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_accel_y(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_YOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_YOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_accel_z(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_ZOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_ZOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_gyro_x(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_GYRO_XOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_XOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_gyro_y(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_GYRO_YOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_YOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_gyro_z(struct icm20608_dev *dev)
{

    int h = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}
static int read_temp(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_TEMP_OUT_H);
    int l = icm20608_read_reg(dev, ICM20608_TEMP_OUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (int16_t)((h << 8) | l);
}

static int icm20608_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int *val, int *val2, long mask)
{
    struct icm20608_dev *data = iio_priv(indio_dev);

    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    mutex_lock(&data->lock);
    switch (chan->type)
    {
    case IIO_ACCEL:
        if (chan->channel == 0)
            *val = read_accel_x(data);
        if (chan->channel == 1)
            *val = read_accel_y(data);
        if (chan->channel == 2)
            *val = read_accel_z(data);
        break;
    case IIO_ANGL_VEL:
        if (chan->channel == 0)
            *val = read_gyro_x(data);
        if (chan->channel == 1)
            *val = read_gyro_y(data);
        if (chan->channel == 2)
            *val = read_gyro_z(data);
        break;
    case IIO_TEMP:
        *val = read_temp(data);
        break;
    default:

        return -EINVAL;
    }

    mutex_unlock(&data->lock);
    return IIO_VAL_INT;
}

static const struct iio_info icm20608_info = {
    .read_raw = icm20608_read_raw,
};

static int icm20608_probe(struct spi_device *spi)
{
    int ret, id;
    struct iio_dev *indio_dev;
    struct icm20608_dev *data;

    // 创建 IIO 设备并分配私有数据
    indio_dev = iio_device_alloc(sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    mutex_init(&data->lock); // <<< 这行要放在 probe 前面
    data->spi = spi;

    // SPI 初始化
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;
    ret = spi_setup(spi);
    if (ret < 0)
    {
        printk("SPI setup failed: %d\n", ret);
        kfree(data);
        iio_device_free(indio_dev);
        return ret;
    }

    printk("icm20608 probe start\n");

    // 复位芯片
    icm20608_write_reg(data, ICM20608_PWR_MGMT_1, 0x80);
    mdelay(50); // 等待复位完成

    // 退出休眠，选择 PLL 时钟源
    icm20608_write_reg(data, ICM20608_PWR_MGMT_1, 0x01);
    mdelay(50);

    // WHO_AM_I 验证
    id = icm20608_read_reg(data, ICM20608_WHO_AM_I);
    printk("ICM20608 WHO_AM_I = 0x%x\n", id);
    if (id != ICM20608G_ID && id != ICM20608D_ID)
    {
        printk("ICM20608 id check error\n");
        kfree(data);
        iio_device_free(indio_dev);
        return -ENODEV;
    }
    printk("ICM20608 id check success\n");

    // 正点原子初始化顺序
    icm20608_write_reg(data, ICM20608_SMPLRT_DIV, 0x00);
    icm20608_write_reg(data, ICM20608_GYRO_CONFIG, 0x18);
    icm20608_write_reg(data, ICM20608_ACCEL_CONFIG, 0x18);
    icm20608_write_reg(data, ICM20608_CONFIG, 0x04);
    icm20608_write_reg(data, ICM20608_ACCEL_CONFIG2, 0x04);
    icm20608_write_reg(data, ICM20608_PWR_MGMT_2, 0x00);
    icm20608_write_reg(data, ICM20608_LP_MODE_CFG, 0x00);
    icm20608_write_reg(data, ICM20608_FIFO_EN, 0x00);
    msleep(100);

    // 配置 IIO device
    indio_dev->name = "icm20608";
    indio_dev->channels = icm20608_channels;
    indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);
    indio_dev->info = &icm20608_info;
    indio_dev->modes = INDIO_DIRECT_MODE;

    // **绑定到 SPI**
    spi_set_drvdata(spi, indio_dev);

    // 注册 IIO 设备
    ret = iio_device_register(indio_dev);
    if (ret < 0)
    {
        printk("iio_device_register failed: %d\n", ret);
        iio_device_free(indio_dev);
        return ret;
    }

    // 读取六轴 + 温度打印
    printk("Accel: X=%d Y=%d Z=%d | Gyro: X=%d Y=%d Z=%d | Temp=%d\n",
           read_accel_x(data), read_accel_y(data),
           read_accel_z(data), read_gyro_x(data),
           read_gyro_y(data), read_gyro_z(data), read_temp(data));
    printk("icm20608 probe finished\n");

    return 0;
}

static int icm20608_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    iio_device_unregister(indio_dev);
    kfree(iio_priv(indio_dev));
    printk("icm20608 remove\n");
    return 0;
}

static const struct spi_device_id icm20608_id[] = {
    {"icm20608", 0},
    {},
};

static const struct of_device_id icm20608_of_match[] = {
    {.compatible = "sensor_collect,icm20608"},
    {},
};

MODULE_DEVICE_TABLE(spi, icm20608_id);

static struct spi_driver icm20608_driver = {
    .driver = {
        .name = "icm20608",
        .owner = THIS_MODULE,
        .of_match_table = icm20608_of_match,
    },
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .id_table = icm20608_id,
};
static int __init icm20608_init(void)
{
    int ret;
    printk("icm20608 driver init\r\n");
    ret = spi_register_driver(&icm20608_driver);
    return ret;
}

static void __exit icm20608_exit(void)
{
    printk("icm20608 driver exit\r\n");
    spi_unregister_driver(&icm20608_driver);
}

module_init(icm20608_init);
module_exit(icm20608_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Farewellove");