/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/12 22:00:29
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/12 22:01:33
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#define LCD_RESET 40 // 对应 schematics 上的 RESET 引脚
#define LCD_DE 46
#define LCD_VSYNC 31
#define LCD_HSYNC 32
#define LCD_PCLK 30

// 数据口 D0-D23 GPIO 数组（按 schematic）
static int lcd_data_gpios[24] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23};

// 初始化 LCD（复位 + GPIO 配置）
static int lcd_init(void)
{
    int i, ret;
    // 初始化控制信号
    ret = gpio_request(LCD_RESET, "lcd_reset");
    if (ret < 0)
        return ret;
    gpio_direction_output(LCD_RESET, 1);

    gpio_request(LCD_DE, "lcd_de");
    gpio_direction_output(LCD_DE, 1);
    gpio_request(LCD_VSYNC, "lcd_vsync");
    gpio_direction_output(LCD_VSYNC, 1);
    gpio_request(LCD_HSYNC, "lcd_hsync");
    gpio_direction_output(LCD_HSYNC, 1);
    gpio_request(LCD_PCLK, "lcd_pclk");
    gpio_direction_output(LCD_PCLK, 1);

    // 初始化数据口
    for (i = 0; i < 24; i++)
    {
        gpio_request(lcd_data_gpios[i], "lcd_data");
        gpio_direction_output(lcd_data_gpios[i], 0);
    }

    // 复位LCD
    gpio_set_value(LCD_RESET, 0);
    mdelay(10);
    gpio_set_value(LCD_RESET, 1);
    mdelay(10);

    pr_info("Minimal LCD driver initialized\n");
    return 0;
}

// 写一像素函数（示例）
static void lcd_write_pixel(uint32_t rgb)
{
    int i;
    for (i = 0; i < 24; i++)
    {
        gpio_set_value(lcd_data_gpios[i], (rgb >> i) & 1);
    }
    // 模拟 PCLK 拉高拉低
    gpio_set_value(LCD_PCLK, 1);
    gpio_set_value(LCD_PCLK, 0);
}

// 驱动 init/exit
static int __init lcd_driver_init(void)
{
    return lcd_init();
}

static void __exit lcd_driver_exit(void)
{
    int i;
    gpio_free(LCD_RESET);
    gpio_free(LCD_DE);
    gpio_free(LCD_VSYNC);
    gpio_free(LCD_HSYNC);
    gpio_free(LCD_PCLK);
    for (i = 0; i < 24; i++)
        gpio_free(lcd_data_gpios[i]);
    pr_info("Minimal LCD driver exited\n");
}

module_init(lcd_driver_init);
module_exit(lcd_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Farewellove");