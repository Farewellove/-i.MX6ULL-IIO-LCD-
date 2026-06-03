#!/bin/sh
# 加载 AP3216C + ICM20608 + GT911 三个驱动模块
# 用法: ./load_drivers.sh

MOD_DIR=/lib/modules/4.1.15

echo "=== Loading drivers ==="

echo "[1/3] ap3216c..."
insmod ${MOD_DIR}/ap3216c.ko
if [ $? -eq 0 ]; then
    echo "  ap3216c loaded"
else
    echo "  ap3216c FAILED or already loaded"
fi

echo "[2/3] icm20608..."
insmod ${MOD_DIR}/icm20608.ko
if [ $? -eq 0 ]; then
    echo "  icm20608 loaded"
else
    echo "  icm20608 FAILED or already loaded"
fi

echo "[3/3] gt911_ts..."
insmod ${MOD_DIR}/gt911_ts.ko
if [ $? -eq 0 ]; then
    echo "  gt911_ts loaded"
else
    echo "  gt911_ts FAILED or already loaded"
fi

echo "=== Done ==="
