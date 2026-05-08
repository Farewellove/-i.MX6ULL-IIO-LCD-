#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include "icm20608reg.h"

struct icm20608_data
{
    struct spi_device *spi;
};

static int icm20608_read_reg(struct icm20608_data *data, u8 reg)
{
    u8 tx[2];
    u8 rx[2];
    int ret;

    tx[0] = reg | ICM20608_REG_READ;
    tx[1] = 0x00;

    ret = spi_write_then_read(data->spi, tx, 1, rx, 1);
    if (ret < 0)
        return ret;

    return rx[0];
}

static int icm20608_write_reg(struct icm20608_data *data, u8 reg, u8 val)
{
    u8 tx[2];

    tx[0] = reg & 0x7F;
    tx[1] = val;

    return spi_write(data->spi, tx, 2);
}

static int icm20608_hw_init(struct icm20608_data *data)
{
    int ret;

    /*
     * 退出 sleep
     */
    ret = icm20608_write_reg(data, ICM20608_PWR_MGMT_1, 0x01);
    if (ret < 0)
        return ret;

    msleep(50);

    /*
     * 打开加速度计和陀螺仪各轴
     */
    ret = icm20608_write_reg(data, ICM20608_PWR_MGMT_2, 0x00);
    if (ret < 0)
        return ret;

    msleep(20);

    return 0;
}

static int icm20608_probe(struct spi_device *spi)
{
    struct icm20608_data *data;
    int ret;
    int whoami;

    printk("icm20608 spi probe\n");

    data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->spi = spi;
    spi_set_drvdata(spi, data);

    /*
     * SPI 基本参数
     */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 1000000;

    ret = spi_setup(spi);
    if (ret < 0)
    {
        dev_err(&spi->dev, "spi setup failed\n");
        return ret;
    }

    ret = icm20608_hw_init(data);
    if (ret < 0)
    {
        dev_err(&spi->dev, "icm20608 init failed\n");
        return ret;
    }

    whoami = icm20608_read_reg(data, ICM20608_WHO_AM_I);
    if (whoami < 0)
    {
        dev_err(&spi->dev, "read who_am_i failed\n");
        return whoami;
    }

    dev_info(&spi->dev, "ICM20608 WHO_AM_I = 0x%02x\n", whoami);

    return 0;
}

static int icm20608_remove(struct spi_device *spi)
{
    printk("icm20608 remove\n");
    return 0;
}

static const struct of_device_id icm20608_of_match[] = {
    {.compatible = "invensense,icm20608"},
    {}};
MODULE_DEVICE_TABLE(of, icm20608_of_match);

static const struct spi_device_id icm20608_id[] = {
    {"icm20608", 0},
    {}};
MODULE_DEVICE_TABLE(spi, icm20608_id);

static struct spi_driver icm20608_driver = {
    .driver = {
        .name = ICM20608_NAME,
        .owner = THIS_MODULE,
        .of_match_table = icm20608_of_match,
    },
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .id_table = icm20608_id,
};

module_spi_driver(icm20608_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FAREWELLOVE");
MODULE_DESCRIPTION("ICM20608 SPI minimal driver");