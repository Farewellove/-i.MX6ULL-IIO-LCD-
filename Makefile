# 内核源码路径
KERNELDIR := /home/why/zdyz/alientek_linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

# 当前目录
PWD := $(shell pwd)

# 要编译的模块
obj-m += ap3216c.o
obj-m += sensor_driver.o

# 每个模块对应的源文件
ap3216c-objs := kernel_drivers/ap3216c_iio/ap3216c.o
sensor_driver-objs := kernel_drivers/sensor_driver/sensor_driver.o

# 头文件路径
ccflags-y += -I$(PWD)/kernel_drivers/ap3216c_iio

APP_CC := arm-linux-gnueabihf-gcc

app:
	$(APP_CC) user_app/test.c -o user_app/test


# 默认目标
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# 清理
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

# NFS目录
NFS_DIR := /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15

install: all
	mkdir -p $(NFS_DIR)
	cp -f *.ko $(NFS_DIR)/
	@echo "============================================="
	@echo "✅ 模块已编译并拷贝到 NFS 目录："
	@echo "$(NFS_DIR)"
	@ls -l $(NFS_DIR)/*.ko
	@echo "============================================="

.PHONY: all clean install app