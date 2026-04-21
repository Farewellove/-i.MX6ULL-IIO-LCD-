/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/9 17:07:17
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/4/21 15:06:14
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/device.h> // 👈 就是它
#include <linux/cdev.h>
#include <linux/interrupt.h> // 中断相关API
#include <linux/timer.h>     // 内核定时器相关API
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/of_gpio.h> // of_get_named_gpio
#include <linux/gpio.h>    // gpio_request
#include <linux/fs.h>      // 字符设备

#define SENSOR_NAME "sensor_collect"
#define SENSOR_CNT 1
#define LEDON '1'
#define LEDOFF '0'

struct sensor_dev
{

    struct device *dev; /* 设备		*/
    int key_gpio;
    int led_gpio;

    // 【新增】字符设备相关（用于生成/dev节点，嵌入结构体，不设全局）
    dev_t devid;
    int major;
    int minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    // 定时器相关
    int irq;
    struct timer_list timer;

    // 【新增】消抖状态标志
    atomic_t debouncing; // 1=正在消抖，0=空闲
    int led_state;       // 记录led状态，防止重复报告
};

static int sensor_open(struct inode *inode, struct file *filp)
{
    struct sensor_dev *sdev = container_of(inode->i_cdev, struct sensor_dev, cdev);
    filp->private_data = sdev;
    return 0;
}

static ssize_t sensor_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    struct sensor_dev *sdev = filp->private_data;
    char status[2] = {0}; // 存储状态：status[0]=LED状态，status[1]=按键状态

    if (!sdev)
        return -ENODEV;

    // 至少需要 2 字节才能返回完整状态
    if (cnt < sizeof(status))
        return -EINVAL; // 或者只返回部分？但通常要求完整

    status[0] = sdev->led_state;                // LED: 0=亮, 1=灭
    status[1] = gpio_get_value(sdev->key_gpio); // KEY: 0=按下, 1=松开

    if (copy_to_user(buf, status, sizeof(status)))
        return -EFAULT;

    return sizeof(status); // 总是返回 2
}

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
    case LEDON:                            // 1
        gpio_set_value(sdev->led_gpio, 0); // 低电平点亮 LED
        sdev->led_state = 0;
        break;

    case LEDOFF:                           // 0
        gpio_set_value(sdev->led_gpio, 1); // 高电平熄灭 LED
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
    struct sensor_dev *sdev = filp->private_data;
    return 0;
}

static const struct file_operations sensor_fops = {
    .owner = THIS_MODULE,
    .open = sensor_open,
    .read = sensor_read,
    .write = sensor_write,
    .release = sensor_release,
};

void sensor_timer_func(unsigned long data)
{
    struct sensor_dev *sdev = (struct sensor_dev *)data;
    int val = gpio_get_value(sdev->key_gpio);

    // 只有稳定按下才执行
    if (val == 0)
    {
        // 翻转灯
        sdev->led_state = !sdev->led_state;
        gpio_set_value(sdev->led_gpio, sdev->led_state);

        printk("KEY PRESSED: 灯状态=%d\n", sdev->led_state);
        dev_info(sdev->dev, "Key pressed!\n");
    }

    // 消抖结束
    atomic_set(&sdev->debouncing, 0);
    enable_irq(sdev->irq);
}

// 【追加】中断服务函数：按键触发中断后，启动消抖定时器
irqreturn_t sensor_irq_handler(int irq, void *data)
{

    struct sensor_dev *sdev = (struct sensor_dev *)data;

    printk("========== 进入中断！！！ ==========\n");

    // ====================== 终极过滤：松开抖动直接丢弃 ======================
    if (gpio_get_value(sdev->key_gpio) == 1)
    { // 现在是高电平 → 松开抖动
        printk("=== 松开抖动，忽略 ===\n");
        return IRQ_HANDLED;
    }

    // 消抖中，忽略
    if (atomic_xchg(&sdev->debouncing, 1) == 1)
    {
        return IRQ_HANDLED;
    }

    disable_irq_nosync(sdev->irq);
    mod_timer(&sdev->timer, jiffies + msecs_to_jiffies(50));

    return IRQ_HANDLED; // 必须返回！
}

static int sensor_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct sensor_dev *sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;
    sdev->dev = &pdev->dev;

    // 1. 分配设备号
    ret = alloc_chrdev_region(&sdev->devid, 0, SENSOR_CNT, SENSOR_NAME);
    if (ret < 0)
    {
        dev_err(&pdev->dev, "Failed to allocate chrdev region\n");
        goto fail_chrdev;
    }
    sdev->major = MAJOR(sdev->devid);
    sdev->minor = MINOR(sdev->devid);
    printk("SENSOR MAJOR=%d ,SENSOR MINOR=%d", sdev->major, sdev->minor);
    // 2. 初始化 cdev
    cdev_init(&sdev->cdev, &sensor_fops);
    ret = cdev_add(&sdev->cdev, sdev->devid, SENSOR_CNT);
    if (ret < 0)
    {
        dev_err(sdev->dev, "fail to add cdev\r\n");
        goto fail_cdev;
    }
    // 3. 创建设备类和节点
    /*成功：返回一个指向 struct class 的指针（非 NULL）。
    失败：返回 错误指针（ERR_PTR），例如 ERR_PTR(-ENOMEM)、ERR_PTR(-EINVAL) 等。
    ⚠️ 注意：不是返回 NULL！ 而是使用内核的 错误指针机制（ERR_PTR / IS_ERR）。*/
    sdev->class = class_create(THIS_MODULE, SENSOR_NAME);
    if (IS_ERR(sdev->class))
    {
        dev_err(sdev->dev, "fail to create class\r\n");
        goto fail_class;
    }
    /*成功：返回一个指向 struct device 的指针（非 NULL）。
    失败：返回 错误指针（ERR_PTR），如 ERR_PTR(-ENOMEM)。*/
    sdev->device = device_create(sdev->class, NULL, sdev->devid, NULL, SENSOR_NAME);
    if (IS_ERR(sdev->device))
    {
        dev_err(sdev->dev, "failt to create device\r\n");
        goto fail_device;
    }
    /*| 宏 | 作用 |
    |----|------|
    | `IS_ERR(ptr)` | 判断指针是否为错误指针（返回 true/false） |
    | `PTR_ERR(ptr)` | 从错误指针中提取具体的错误码（如 `-ENOMEM`） |*/

    // 4. 获取 GPIO
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

    // 5、申请gpio
    // 【新增】GPIO申请与配置（关键：避免引脚冲突）
    /*✅ 成功时：
    返回 0
    ❌ 失败时：
    返回 负的错误码（negative errno），例如：
    -EBUSY：GPIO 已被其他驱动占用
    -EINVAL：GPIO 编号无效
    -ENODEV：GPIO 控制器未注册或不存在
    其他标准内核错误码（如 -ENOMEM 等，较少见）*/
    ret = gpio_request(sdev->key_gpio, "key-gpio"); // 申请按键GPIO
    if (ret < 0)
    {
        pr_err("fail to request key-gpio\r\n");
        goto fail_gpio;
    }
    ret = gpio_request(sdev->led_gpio, "led-gpio"); // 申请LED GPIO
    if (ret < 0)
    {
        pr_err("fail to request led-gpio\r\n");
        goto fail_gpio;
    }
    /*✅ 成功时：
    返回 0
    ❌ 失败时：
    返回 负的错误码（negative errno），例如：
    -EINVAL：GPIO 编号无效
    -EBUSY：GPIO 未被申请（未调用 gpio_request()）或方向已固定
    -ENODEV：GPIO 控制器不存在或未启用
    其他平台相关错误（如硬件不支持输出模式）*/
    ret = gpio_direction_input(sdev->key_gpio); // 按键设为输入模式
    if (ret < 0)
    {
        pr_err("fail to direct key-gpio\r\n");
        goto fail_request;
    }
    ret = gpio_direction_output(sdev->led_gpio, 1); // LED设为输出，默认关闭
    if (ret < 0)
    {
        pr_err("fail to direct led-gpio\r\n");
        goto fail_request;
    }
    sdev->led_state = 1;

    atomic_set(&sdev->debouncing, 0);

    // 初始化定时器和中断
    // 1.初始化定时器
    setup_timer(&sdev->timer, sensor_timer_func, (unsigned long)sdev);

    // 2. 将GPIO转换为中断号（IMX6ULL GPIO与中断号一一对应）
    sdev->irq = gpio_to_irq(sdev->key_gpio);
    if (sdev->irq < 0)
    {
        dev_err(sdev->dev, "gpio to irq failed\n");
        ret = -EINVAL;
        goto fail_irq;
    }

    // 3. 注册中断
    ret = request_irq(sdev->irq, sensor_irq_handler,
                      IRQF_TRIGGER_FALLING, // 下降沿
                      "sensor_irq",         // 中断名称（用于cat /proc/interrupts查看）
                      sdev);                // 传递设备结构体（中断服务函数中使用）
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

static int sensor_remove(struct platform_device *pdev)
{
    struct sensor_dev *sdev = platform_get_drvdata(pdev); // ← 找回 sdev！
    // 然后可以访问 sdev->key_gpio, sdev->dev 等

    // 注销irq
    free_irq(sdev->irq, sdev);
    // 删除定时器
    del_timer_sync(&sdev->timer);
    // 关灯
    gpio_set_value(sdev->led_gpio, 1);
    // 释放gpio
    gpio_free(sdev->key_gpio);
    gpio_free(sdev->led_gpio);
    // 注销
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
/*将 sensor_of_match 表导出到模块的 ELF 段（.modinfo）中，使得用户空间工具（如 modprobe、udev）或内核模块加载系统能够知道：
“当设备树中出现 compatible = "my,sensor-collect"; 的节点时，应该自动加载 sensor_driver.ko 模块”。 */
MODULE_DEVICE_TABLE(of, sensor_of_match); // 导出给内核模块系统

static struct platform_driver sensor_driver = {
    .probe = sensor_probe,
    .remove = sensor_remove,
    .driver = {
        .name = "sensor_collect",
        .of_match_table = sensor_of_match,
    },

};

static int __init sensor_init(void)
{
    return platform_driver_register(&sensor_driver);
}

static void __exit sensor_exit(void)
{
    return platform_driver_unregister(&sensor_driver);
}

module_init(sensor_init);
module_exit(sensor_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FAREWELLOVE");
