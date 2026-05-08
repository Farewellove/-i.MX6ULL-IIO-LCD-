#!/bin/sh
cd /lib/modules/4.1.15
depmod
modprobe ap3216c
ls /sys/bus/iio/devices/
