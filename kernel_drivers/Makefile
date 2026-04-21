# 内核源码路径（必须指向你实际的内核目录）
KERNELDIR := /home/why/zdyz/alientek_linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

# 当前目录
PWD := $(shell pwd)

# 要编译的模块
obj-m += key_led_drv.o
#obj-m += ap3216c_iio.o
#obj-m += icm20608_iio.o
#obj-m += lcd_spi.o

# 每个模块对应的源文件
key_led_drv-objs  := key_led_drv/key_led_drv.o
#ap3216c_iio-objs  := ap3216c_iio/ap3216c_iio.o
#icm20608_iio-objs := icm20608_iio/icm20608_iio.o
#lcd_spi-objs      := lcd_spi/lcd_spi.o lcd_spi/lcd_font.o

# 头文件目录
#ccflags-y += -I$(PWD)/include

# 默认目标：编译模块
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# 清理编译产物
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

# 自动拷贝到 NFS
NFS_DIR := /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15

install: all
	mkdir -p $(NFS_DIR)
	cp -f *.ko $(NFS_DIR)/
	@echo "============================================="
	@echo "✅ 模块已编译并拷贝到 NFS 目录："
	@echo "$(NFS_DIR)"
	@ls -l $(NFS_DIR)/*.ko
	@echo "============================================="

.PHONY: all clean install