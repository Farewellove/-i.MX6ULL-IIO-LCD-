/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/21 15:02:44
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/9 17:08:49
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include "icm20608reg.h"
#include <linux/delay.h>

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
    tx[0] = reg & 0x7F; // 写操作
    tx[1] = val;
    return spi_write(dev->spi, tx, 2);
}

int read_accel_x(struct icm20608_dev *dev)
{
    int high = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_H);
    int low = icm20608_read_reg(dev, ICM20608_ACCEL_XOUT_L);
    if (high < 0 || low < 0)
        return -EIO;

    return (high << 8) | low; // 拼成16位
}

int read_gyro_z(struct icm20608_dev *dev)
{
    int high = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_H);
    int low = icm20608_read_reg(dev, ICM20608_GYRO_ZOUT_L);

    if (high < 0 || low < 0)
        return -EIO;

    return (high << 8) | low;
}

int read_temp(struct icm20608_dev *dev)
{
    int high = icm20608_read_reg(dev, ICM20608_TEMP_OUT_H);
    int low = icm20608_read_reg(dev, ICM20608_TEMP_OUT_L);

    if (high < 0 || low < 0)
        return -EIO;

    return (high << 8) | low;
}

static int icm20608_probe(struct spi_device *spi)
{
    int ret, id;
    struct icm20608_dev *dev;
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
    {
        return -ENOMEM;
    }
    dev->spi = spi; // 保存spi设备指针
    spi_set_drvdata(spi, dev);

    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;

    printk("icm20608 probe success\r\n");
    id = icm20608_read_reg(dev, ICM20608_WHO_AM_I);
    if (id < 0)
    {
        printk("icm20608 read who_am_i failed\n");
        kfree(dev);
        return id;
    }

    printk("icm20608 WHO_AM_I = 0x%x\n", id);

    if (id != ICM20608G_ID && id != ICM20608D_ID)
    {
        printk("icm20608 id check error\n");
        kfree(dev);
        return -ENODEV;
    }

    printk("icm20608 id check success\n");

    // 初始化芯片
    icm20608_write_reg(dev, ICM20608_PWR_MGMT_1, 0x01);
    msleep(50);

    icm20608_write_reg(dev, ICM20608_PWR_MGMT_2, 0x00);
    msleep(50);

    icm20608_write_reg(dev, ICM20608_ACCEL_CONFIG, 0x00); // ±2g
    icm20608_write_reg(dev, ICM20608_GYRO_CONFIG, 0x00);  // ±250°/s

    printk("accel_x = %d, gyro_z = %d, temp = %d\n",
           read_accel_x(dev),
           read_gyro_z(dev),
           read_temp(dev));

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