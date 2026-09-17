#!/bin/sh
# 在开发板上单独加载 ap3216c 驱动，并列出 IIO 设备节点确认注册成功
cd /lib/modules/4.1.15
depmod
modprobe ap3216c
ls /sys/bus/iio/devices/
