/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/9 17:07:17
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/12 22:00:20
 * @Description: 传感器采集综合驱动 (platform driver)
 *               LED 控制 + 按键中断 + 字符设备 (/dev/sensor_collect)
 *               LED=GPIO1_IO03, KEY=GPIO1_IO18 (带消抖)
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/fs.h>

#define SENSOR_NAME "sensor_collect"
#define SENSOR_CNT  1
#define LEDON  '1'
#define LEDOFF '0'

/*
 * 私有数据结构：包含字符设备、GPIO、中断、消抖定时器等
 * 使用 platform driver 模型，设备信息来自设备树
 */
struct sensor_dev
{
    struct device *dev;           /* platform 设备指针 */
    int key_gpio;                 /* 按键 GPIO 编号 (旧 API) */
    int led_gpio;                 /* LED GPIO 编号 (旧 API) */

    /* 字符设备 (生成 /dev/sensor_collect) */
    dev_t devid;
    int major;
    int minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    /* 中断 + 消抖定时器 */
    int irq;
    struct timer_list timer;
    atomic_t debouncing;          /* 1=正在消抖中, 0=空闲 */
    int led_state;                /* 0=亮, 1=灭 */
};

/* ── 字符设备 fops ── */

static int sensor_open(struct inode *inode, struct file *filp)
{
    struct sensor_dev *sdev = container_of(inode->i_cdev, struct sensor_dev, cdev);
    filp->private_data = sdev;
    return 0;
}

/*
 * read 返回 2 字节：led_state + key_state
 * 用户态读取后可以判断 LED 和按键的当前状态
 */
static ssize_t sensor_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    struct sensor_dev *sdev = filp->private_data;
    char status[2] = {0};

    if (!sdev)
        return -ENODEV;
    if (cnt < sizeof(status))
        return -EINVAL;

    status[0] = sdev->led_state;                /* LED: 0=亮, 1=灭 */
    status[1] = gpio_get_value(sdev->key_gpio); /* KEY: 0=按下, 1=松开 */

    if (copy_to_user(buf, status, sizeof(status)))
        return -EFAULT;

    return sizeof(status);
}

/*
 * write 接收单字节命令：'1' 开灯, '0' 关灯
 * LED 低电平点亮（正点原子底板设计）
 */
static ssize_t sensor_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    struct sensor_dev *sdev = filp->private_data;
    char data;

    if (!sdev)
        return -ENODEV;
    if (cnt < 1)
        return -EINVAL;
    if (copy_from_user(&data, buf, 1))
        return -EFAULT;

    switch (data)
    {
    case LEDON:
        gpio_set_value(sdev->led_gpio, 0);  /* 低电平点亮 */
        sdev->led_state = 0;
        break;
    case LEDOFF:
        gpio_set_value(sdev->led_gpio, 1);  /* 高电平熄灭 */
        sdev->led_state = 1;
        break;
    default:
        pr_err("Invalid command: %d (expected '0' or '1')\n", data);
        return -EINVAL;
    }
    return cnt;
}

static int sensor_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations sensor_fops = {
    .owner   = THIS_MODULE,
    .open    = sensor_open,
    .read    = sensor_read,
    .write   = sensor_write,
    .release = sensor_release,
};

/*
 * 消抖定时器回调：50ms 后再次读取按键电平
 * 如果仍然为低 → 确认按下 → 翻转 LED 状态
 * 处理完后重新使能中断
 */
void sensor_timer_func(unsigned long data)
{
    struct sensor_dev *sdev = (struct sensor_dev *)data;

    if (gpio_get_value(sdev->key_gpio) == 0)
    {
        sdev->led_state = !sdev->led_state;
        gpio_set_value(sdev->led_gpio, sdev->led_state);
        printk("KEY PRESSED: 灯状态=%d\n", sdev->led_state);
        dev_info(sdev->dev, "Key pressed!\n");
    }

    atomic_set(&sdev->debouncing, 0);     /* 消抖结束 */
    enable_irq(sdev->irq);                /* 重新开中断 */
}

/*
 * 按键中断处理：下降沿触发 → 软件过滤抖动 → 启动 50ms 定时器
 *
 * 消抖策略：中断来临时先读取电平：
 *   - 如果已经是高电平 → 松开抖动 → 忽略
 *   - 如果是低电平 → 可能是真实按下 → 关中断 + 启动 50ms 定时器再次确认
 */
irqreturn_t sensor_irq_handler(int irq, void *data)
{
    struct sensor_dev *sdev = (struct sensor_dev *)data;

    printk("========== 进入中断！！！ ==========\n");

    if (gpio_get_value(sdev->key_gpio) == 1)   /* 松开抖动 */
    {
        printk("=== 松开抖动，忽略 ===\n");
        return IRQ_HANDLED;
    }

    if (atomic_xchg(&sdev->debouncing, 1) == 1) /* 已在消抖中 */
        return IRQ_HANDLED;

    disable_irq_nosync(sdev->irq);              /* 关中断防止重入 */
    mod_timer(&sdev->timer, jiffies + msecs_to_jiffies(50));

    return IRQ_HANDLED;
}

static int sensor_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct sensor_dev *sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;
    sdev->dev = &pdev->dev;

    /* 1. 动态分配字符设备号 */
    ret = alloc_chrdev_region(&sdev->devid, 0, SENSOR_CNT, SENSOR_NAME);
    if (ret < 0)
    {
        dev_err(&pdev->dev, "Failed to allocate chrdev region\n");
        goto fail_chrdev;
    }
    sdev->major = MAJOR(sdev->devid);
    sdev->minor = MINOR(sdev->devid);
    printk("SENSOR MAJOR=%d ,SENSOR MINOR=%d", sdev->major, sdev->minor);

    /* 2. 初始化 cdev 并注册到内核 */
    cdev_init(&sdev->cdev, &sensor_fops);
    ret = cdev_add(&sdev->cdev, sdev->devid, SENSOR_CNT);
    if (ret < 0)
    {
        dev_err(sdev->dev, "fail to add cdev\r\n");
        goto fail_cdev;
    }

    /* 3. 创建 /sys/class + /dev 设备节点 */
    sdev->class = class_create(THIS_MODULE, SENSOR_NAME);
    if (IS_ERR(sdev->class))
    {
        dev_err(sdev->dev, "fail to create class\r\n");
        goto fail_class;
    }
    sdev->device = device_create(sdev->class, NULL, sdev->devid, NULL, SENSOR_NAME);
    if (IS_ERR(sdev->device))
    {
        dev_err(sdev->dev, "fail to create device\r\n");
        goto fail_device;
    }

    /* 4. 从设备树获取 GPIO 编号（旧 API：of_get_named_gpio） */
    sdev->key_gpio = of_get_named_gpio(sdev->dev->of_node, "key-gpio", 0);
    if (!gpio_is_valid(sdev->key_gpio))
    {
        dev_err(sdev->dev, "Invalid key-gpio\n");
        ret = -EINVAL;
        goto fail_gpio;
    }
    sdev->led_gpio = of_get_named_gpio(sdev->dev->of_node, "led-gpio", 0);
    if (!gpio_is_valid(sdev->led_gpio))
    {
        dev_err(sdev->dev, "Invalid led-gpio\n");
        ret = -EINVAL;
        goto fail_gpio;
    }

    /* 5. 申请并配置 GPIO */
    ret = gpio_request(sdev->key_gpio, "key-gpio");
    if (ret < 0)
    {
        pr_err("fail to request key-gpio\r\n");
        goto fail_gpio;
    }
    ret = gpio_request(sdev->led_gpio, "led-gpio");
    if (ret < 0)
    {
        pr_err("fail to request led-gpio\r\n");
        goto fail_gpio;
    }
    ret = gpio_direction_input(sdev->key_gpio);
    if (ret < 0)
    {
        pr_err("fail to direct key-gpio\r\n");
        goto fail_request;
    }
    ret = gpio_direction_output(sdev->led_gpio, 1);  /* LED 初始关闭 */
    if (ret < 0)
    {
        pr_err("fail to direct led-gpio\r\n");
        goto fail_request;
    }
    sdev->led_state = 1;
    atomic_set(&sdev->debouncing, 0);

    /* 6. 初始化消抖定时器 */
    setup_timer(&sdev->timer, sensor_timer_func, (unsigned long)sdev);

    /* 7. GPIO → IRQ 号转换，注册中断（下降沿触发） */
    sdev->irq = gpio_to_irq(sdev->key_gpio);
    if (sdev->irq < 0)
    {
        dev_err(sdev->dev, "gpio to irq failed\n");
        ret = -EINVAL;
        goto fail_irq;
    }
    ret = request_irq(sdev->irq, sensor_irq_handler,
                      IRQF_TRIGGER_FALLING, "sensor_irq", sdev);
    if (ret < 0)
    {
        dev_err(sdev->dev, "failed to request irq\n");
        goto fail_irq;
    }

    platform_set_drvdata(pdev, sdev);
    dev_info(&pdev->dev, "sensor probe success\n");
    return 0;

fail_irq:
    del_timer_sync(&sdev->timer);
fail_request:
    gpio_free(sdev->key_gpio);
    gpio_free(sdev->led_gpio);
fail_gpio:
    device_destroy(sdev->class, sdev->devid);
fail_device:
    class_destroy(sdev->class);
fail_class:
    cdev_del(&sdev->cdev);
fail_cdev:
    unregister_chrdev_region(sdev->devid, SENSOR_CNT);
fail_chrdev:
    return ret;
}

/*
 * remove 清理顺序与 probe 相反：先关中断 → 删定时器 → 关灯 → 释放 GPIO → 注销设备
 */
static int sensor_remove(struct platform_device *pdev)
{
    struct sensor_dev *sdev = platform_get_drvdata(pdev);

    free_irq(sdev->irq, sdev);
    del_timer_sync(&sdev->timer);
    gpio_set_value(sdev->led_gpio, 1);       /* 关灯 */
    gpio_free(sdev->key_gpio);
    gpio_free(sdev->led_gpio);
    device_destroy(sdev->class, sdev->devid);
    class_destroy(sdev->class);
    cdev_del(&sdev->cdev);
    unregister_chrdev_region(sdev->devid, SENSOR_CNT);

    printk("=== sensor_remove 成功 ===\r\n");
    return 0;
}

static const struct of_device_id sensor_of_match[] = {
    {.compatible = "my,sensor_collect"},
    {},
};

/*
 * MODULE_DEVICE_TABLE 将匹配表导出到模块信息中，
 * 当设备树中出现 compatible="my,sensor_collect" 的节点时，
 * 内核自动加载此模块。
 */
MODULE_DEVICE_TABLE(of, sensor_of_match);

static struct platform_driver sensor_driver = {
    .probe  = sensor_probe,
    .remove = sensor_remove,
    .driver = {
        .name           = "sensor_collect",
        .of_match_table = sensor_of_match,
    },
};

/*
 * platform driver 通过 compatible 字符串与设备树节点匹配，
 * 匹配成功后自动调用 probe。
 */
static int __init sensor_init(void)
{
    return platform_driver_register(&sensor_driver);
}

static void __exit sensor_exit(void)
{
    platform_driver_unregister(&sensor_driver);
}

module_init(sensor_init);
module_exit(sensor_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FAREWELLOVE");
