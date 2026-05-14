# 内核源码路径
KERNELDIR := /home/why/zdyz/alientek_linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

# 当前目录
PWD := $(shell pwd)

# 输出目录
OUT_MODULES := $(PWD)/output/modules
OUT_BIN     := $(PWD)/output/bin

# NFS目录
NFS_DIR := /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15

# 用户程序交叉编译器
APP_CC := arm-linux-gnueabihf-gcc

# 要编译的模块
obj-m += ap3216c.o
obj-m += sensor_driver.o
obj-m += icm20608.o
obj-m +=lcd.o

# 每个模块对应的源文件
ap3216c-objs := kernel_drivers/ap3216c_iio/ap3216c.o
sensor_driver-objs := kernel_drivers/sensor_driver/sensor_driver.o
icm20608-objs:=kernel_drivers/icm20608_iio/icm20608.o
lcd-objs:=kernel_drivers/lcd_spi/lcd.o

# 头文件路径
ccflags-y += -I$(PWD)/kernel_drivers/ap3216c_iio

# 默认目标：只编译内核模块
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# 编译用户态程序
app:
	mkdir -p $(OUT_BIN)
	$(APP_CC) user_app/read_all_sensor.c -o $(OUT_BIN)/read_all_sensor
	$(APP_CC) user_app/ap3216cAPP.c -o $(OUT_BIN)/ap3216cAPP
	$(APP_CC) user_app/icm20608APP.c -o $(OUT_BIN)/icm20608APP

# 编译并安装到 output + NFS
install: all app
	mkdir -p $(OUT_MODULES)
	mkdir -p $(OUT_BIN)
	mkdir -p $(NFS_DIR)

	cp -f *.ko $(OUT_MODULES)/
	cp -f *.ko $(NFS_DIR)/
	cp -f $(OUT_BIN)/read_all_sensor  $(NFS_DIR)/read_all_sensor
	cp -f $(OUT_BIN)/ap3216cAPP  $(NFS_DIR)/ap3216cAPP
	cp -f $(OUT_BIN)/icm20608APP  $(NFS_DIR)/icm20608APP

	@echo "============================================="
	@echo "✅ 模块已输出到：$(OUT_MODULES)"
	@ls -l $(OUT_MODULES)/*.ko
	@echo "✅ 用户程序已输出到：$(OUT_BIN)"
	@ls -l $(OUT_BIN)
	@echo "✅ 已同步到 NFS：$(NFS_DIR)"
	@echo "============================================="

# 清理
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -rf $(OUT_MODULES)/*.ko
	rm -rf $(OUT_BIN)/test

.PHONY: all app install clean