/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/21 15:02:44
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/8 22:00:13
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include "icm20608reg.h"

static int icm20608_probe(struct spi_device *spi)
{
    printk("icm20608 probe success\r\n");

    return 0;
}

static int icm20608_remove(struct spi_device *spi)
{

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