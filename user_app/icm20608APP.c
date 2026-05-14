#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // usleep
#include <fcntl.h>  // open
#include <string.h> // memset
#include <errno.h>
#include <stdint.h> // C99 标准类型定义

#define SYSFS_IIO_PATH "/sys/bus/iio/devices/iio:device0/"

// 定义节点名称
const char *channels[] = {
    "in_accel0_raw",
    "in_accel1_raw",
    "in_accel2_raw",
    "in_anglvel0_raw",
    "in_anglvel1_raw",
    "in_anglvel2_raw",
    "in_temp0_raw",
};

// 节点个数
#define NUM_CHANNELS (sizeof(channels) / sizeof(channels[0]))

// 读取单个 sysfs 节点原始值
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

    return atoi(buf); // 转成整数
}

float convert_accel(int16_t raw)
{
    return raw / 2048.0f;
} // ±16g
float convert_gyro(int16_t raw)
{
    return raw / 16.4f;
} // ±2000°/s
float convert_temp(int16_t raw)
{
    return ((float)raw / 326.8f) + 25;
}

int main()
{
    char path[128];

    while (1)
    {
        // 读取加速度
        int ax = read_sysfs_int(SYSFS_IIO_PATH "in_accel0_raw");
        int ay = read_sysfs_int(SYSFS_IIO_PATH "in_accel1_raw");
        int az = read_sysfs_int(SYSFS_IIO_PATH "in_accel2_raw");

        // 读取陀螺仪
        int gx = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel0_raw");
        int gy = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel1_raw");
        int gz = read_sysfs_int(SYSFS_IIO_PATH "in_anglvel2_raw");

        // 读取温度
        int temp_raw = read_sysfs_int(SYSFS_IIO_PATH "in_temp0_raw");

        // 转换单位
        float ax_g = convert_accel(ax);
        float ay_g = convert_accel(ay);
        float az_g = convert_accel(az);

        float gx_dps = convert_gyro(gx);
        float gy_dps = convert_gyro(gy);
        float gz_dps = convert_gyro(gz);

        float temp_c = convert_temp(temp_raw);

        // 整齐打印
        printf("Accel: X=%.2fg  Y=%.2fg  Z=%.2fg | ", ax_g, ay_g, az_g);
        printf("Gyro: X=%.2f°/s  Y=%.2f°/s  Z=%.2f°/s | ", gx_dps, gy_dps, gz_dps);
        printf("Temp: %.2f°C\n", temp_c);

        sleep(1); // 每 1s 读取一次
    }

    return 0;
}