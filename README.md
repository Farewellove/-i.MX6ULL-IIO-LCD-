# i.MX6ULL AP3216C IIO Sensor Driver

基于 正点原子 i.MX6ULL 开发板 的 Linux IIO 传感器驱动项目。

当前项目实现：

- AP3216C I2C 传感器驱动
- Linux IIO 子系统接入
- ALS / PS / IR 三通道支持
- RAW / SCALE 数据接口
- 用户态实时采集程序

---

# 项目结构

```text
sensor_collect/
├── kernel_drivers/
│   ├── ap3216c_iio/
│   │   ├── ap3216c.c
│   │   └── ap3216creg.h
│   │
│   └── sensor_driver/
│       └── sensor_driver.c
│
├── user_app/
│   └── test.c
│
├── Makefile
└── README.md
```

---

# 开发环境

## 硬件平台

- 正点原子 i.MX6ULL 开发板
- AP3216C 光照/接近/红外传感器

## 软件环境

- Linux Kernel 4.1.15
- Ubuntu 20.04
- arm-linux-gnueabihf-gcc

---

# 内核配置

需要开启 Linux IIO 支持：

```text
Device Drivers
    -> Industrial I/O support
        [*] Industrial I/O support
```

确认：

```bash
grep CONFIG_IIO .config
```

输出：

```bash
CONFIG_IIO=y
```

---

# 驱动功能

当前支持：

| 通道 | sysfs接口 |
|------|------------|
| ALS | in_illuminance_raw |
| ALS Scale | in_illuminance_scale |
| PS | in_proximity_raw |
| PS Scale | in_proximity_scale |
| IR | in_intensity_ir_raw |
| IR Scale | in_intensity_ir_scale |

---

# 编译驱动

```bash
make
```

安装到 NFS：

```bash
make install
```

---

# 加载驱动

开发板：

```bash
cd /lib/modules/4.1.15
depmod
modprobe ap3216c
```

---

# IIO 设备验证

```bash
ls /sys/bus/iio/devices/
```

输出：

```bash
iio:device0
```

查看设备：

```bash
cat /sys/bus/iio/devices/iio:device0/name
```

输出：

```bash
ap3216c
```

---

# 用户态测试程序

编译：

```bash
arm-linux-gnueabihf-gcc user_app/test.c -o test
```

运行：

```bash
./test
```

示例输出：

```text
ALS: raw=12, scale=1, value=12 lux
PS : raw=324, scale=1, value=324
IR : raw=258, scale=1, value=258
```

---

# 技术要点

本项目涉及：

- Linux I2C 子系统
- Linux IIO 子系统
- sysfs
- 内核模块开发
- I2C SMBus 通信
- RAW / SCALE 数据抽象
- 用户态传感器采集

---

# 后续计划

- [ ] ICM20608 IIO 驱动
- [ ] LCD 实时数据显示
- [ ] IIO Buffer 支持
- [ ] Trigger 模式
- [ ] 多传感器融合采集

---

# 作者

Farewellove