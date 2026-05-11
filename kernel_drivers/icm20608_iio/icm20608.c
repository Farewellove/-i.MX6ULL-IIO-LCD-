/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/21 15:02:44
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/11 21:15:46
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

static struct icm20608_dev
{

    struct spi_device *spi;
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

// 六轴 + 温度读取函数
static int read_accel_x(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_accel_y(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_YOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_YOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_accel_z(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_ACCEL_ZOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_ACCEL_ZOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_gyro_x(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_GYRO_XOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_XOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_gyro_y(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_GYRO_YOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_YOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_gyro_z(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_H);
    int l = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}
static int read_temp(struct icm20608_dev *dev)
{
    int h = icm20608_read_reg(dev, ICM20608_TEMP_OUT_H);
    int l = icm20608_read_reg(dev, ICM20608_TEMP_OUT_L);
    if (h < 0 || l < 0)
        return -EIO;
    return (h << 8) | l;
}

static int icm20608_probe(struct spi_device *spi)
{
    struct icm20608_dev *dev;
    int id, ret, val;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->spi = spi;
    spi_set_drvdata(spi, dev);

    // SPI 初始化
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;
    ret = spi_setup(spi);
    if (ret < 0)
    {
        printk("SPI setup failed: %d\n", ret);
        kfree(dev);
        return ret;
    }

    printk("icm20608 probe start\n");

    // 复位芯片
    ret = icm20608_write_reg(dev, ICM20608_PWR_MGMT_1, 0x80);
    if (ret < 0)
        printk("write PWR_MGMT_1 reset failed: %d\n", ret);
    msleep(50);

    // 退出休眠，选择时钟源
    ret = icm20608_write_reg(dev, ICM20608_PWR_MGMT_1, 0x01);
    if (ret < 0)
        printk("write PWR_MGMT_1 wake failed: %d\n", ret);
    msleep(150);

    // WHO_AM_I 验证
    id = icm20608_read_reg(dev, ICM20608_WHO_AM_I);
    if (id < 0)
    {
        printk("read WHO_AM_I failed\n");
        kfree(dev);
        return id;
    }
    printk("WHO_AM_I = 0x%x\n", id);
    if (id != ICM20608G_ID && id != ICM20608D_ID)
    {
        printk("id check error\n");
        kfree(dev);
        return -ENODEV;
    }
    printk("id check success\n");

    // 配置采样率和滤波
    icm20608_write_reg(dev, ICM20608_SMPLRT_DIV, 0x00);
    icm20608_write_reg(dev, ICM20608_GYRO_CONFIG, 0x18);   // ±2000 dps
    icm20608_write_reg(dev, ICM20608_ACCEL_CONFIG, 0x18);  // ±16g
    icm20608_write_reg(dev, ICM20608_CONFIG, 0x04);        // 陀螺仪低通滤波 BW=20Hz
    icm20608_write_reg(dev, ICM20608_ACCEL_CONFIG2, 0x04); // 加速度低通滤波 BW=21.2Hz

    // 开启所有轴
    icm20608_write_reg(dev, ICM20608_PWR_MGMT_2, 0x00);
    icm20608_write_reg(dev, ICM20608_LP_MODE_CFG, 0x00); // 关闭低功耗
    icm20608_write_reg(dev, ICM20608_FIFO_EN, 0x00);     // 关闭 FIFO
    msleep(100);

    // 读取六轴 + 温度打印
    printk("Accel: X=%d Y=%d Z=%d | Gyro: X=%d Y=%d Z=%d | Temp=%d\n",
           read_accel_x(dev), read_accel_y(dev), read_accel_z(dev),
           read_gyro_x(dev), read_gyro_y(dev), read_gyro_z(dev),
           read_temp(dev));

    printk("icm20608 probe finished\n");

    return 0;
}

static int icm20608_remove(struct spi_device *spi)
{

    struct icm20608_dev *dev = spi_get_drvdata(spi);
    kfree(dev);
    printk("icm20608 remove\r\n");
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