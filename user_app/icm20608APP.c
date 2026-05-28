/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/13 21:36:57
 * @Description: ICM20608 用户态测试程序
 *               通过 IIO sysfs 读取加速度/陀螺仪/温度原始值并转换为物理单位
 *               量程：加速度 ±16g, 陀螺仪 ±2000°/s
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define SYSFS_IIO_PATH "/sys/bus/iio/devices/iio:device0/"

/* IIO 通道 sysfs 文件名 */
const char *channels[] = {
    "in_accel0_raw",       /* X 轴加速度 */
    "in_accel1_raw",       /* Y 轴加速度 */
    "in_accel2_raw",       /* Z 轴加速度 */
    "in_anglvel0_raw",     /* X 轴角速度 */
    "in_anglvel1_raw",     /* Y 轴角速度 */
    "in_anglvel2_raw",     /* Z 轴角速度 */
    "in_temp0_raw",        /* 温度 */
};
#define NUM_CHANNELS (sizeof(channels) / sizeof(channels[0]))

/* 从 sysfs 文件读取整数值 */
int read_sysfs_int(const char *filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        return -1;
    }

    char buf[32];
    memset(buf, 0, sizeof(buf));
    int ret = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (ret <= 0)
    {
        perror("read");
        return -1;
    }

    return atoi(buf);
}

/*
 * 单位转换公式（基于 ±16g / ±2000°/s 量程）：
 *   加速度: g = raw / 2048
 *   陀螺仪: °/s = raw / 16.4
 *   温度:   °C = raw / 326.8 + 25
 */
float convert_accel(int16_t raw)  { return raw / 2048.0f; }
float convert_gyro(int16_t raw)   { return raw / 16.4f;  }
float convert_temp(int16_t raw)   { return ((float)raw / 326.8f) + 25; }

int main()
{
    char path[128];

    while (1)
    {
        /* 读取传感器原始值 */
        int ax = read_sysfs_int(SYSFS_IIO_PATH "in_accel0_raw");
        int ay = read_sysfs_int(SYSFS_IIO_PATH "in_accel1_raw");
        int az = read_sysfs_int(SYSFS_IIO_PATH "in_accel2_raw");

        int gx = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel0_raw");
        int gy = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel1_raw");
        int gz = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel2_raw");

        int temp_raw = read_sysfs_int(SYSFS_IIO_PATH "in_temp0_raw");

        /* 转换为物理单位 */
        float ax_g   = convert_accel(ax);
        float ay_g   = convert_accel(ay);
        float az_g   = convert_accel(az);
        float gx_dps = convert_gyro(gx);
        float gy_dps = convert_gyro(gy);
        float gz_dps = convert_gyro(gz);
        float temp_c = convert_temp(temp_raw);

        printf("Accel: X=%.2fg  Y=%.2fg  Z=%.2fg | ", ax_g, ay_g, az_g);
        printf("Gyro: X=%.2f°/s  Y=%.2f°/s  Z=%.2f°/s | ", gx_dps, gy_dps, gz_dps);
        printf("Temp: %.2f°C\n", temp_c);

        sleep(1);
    }

    return 0;
}
