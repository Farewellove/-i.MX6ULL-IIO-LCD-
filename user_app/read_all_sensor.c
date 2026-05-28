/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/13 09:56:12
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/14 16:47:51
 * @Description: 传感器综合采集用户态程序
 *               同时读取 ICM20608 (6 轴 + 温度)、AP3216C (ALS/PS/IR)
 *               和 sensor_collect (LED/KEY) 的数据并打印
 *               通过 IIO sysfs 和字符设备 /dev/sensor_collect 读取
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#define MAX_PATH_LEN 256
#define IIO_PATH "/sys/bus/iio/devices"

// LED/KEY 字符设备路径
#define SENSOR_DEV "/dev/sensor_collect"

volatile sig_atomic_t stop = 0;
void handle_sigint(int sig)
{
    stop = 1;
}

// ICM20608 六轴 + 温度节点
const char *icm_channels[] = {
    "in_accel0_raw",
    "in_accel1_raw",
    "in_accel2_raw",
    "in_anglvel0_raw",
    "in_anglvel1_raw",
    "in_anglvel2_raw",
    "in_temp0_raw",
};
#define ICM_NUM_CHANNELS (sizeof(icm_channels) / sizeof(icm_channels[0]))

// AP3216C 光传感器节点
const char *ap_channels_raw[] = {
    "in_illuminance_raw",
    "in_proximity_raw",
    "in_intensity_ir_raw",
};
const char *ap_channels_scale[] = {
    "in_illuminance_scale",
    "in_proximity_scale",
    "in_intensity_ir_scale",
};
#define AP_NUM_CHANNELS (sizeof(ap_channels_raw) / sizeof(ap_channels_raw[0]))

// 读取整数 sysfs
int read_sysfs_int(const char *filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    char buf[32] = {0};
    if (read(fd, buf, sizeof(buf) - 1) <= 0)
    {
        close(fd);
        return -1;
    }
    close(fd);
    buf[strcspn(buf, "\r\n")] = 0;
    return atoi(buf);
}

// 转换 ICM20608 单位
float conv_accel(int raw)
{
    return raw / 2048.0f;
}
float conv_gyro(int raw)
{
    return raw / 16.4f;
}
float conv_temp(int raw)
{
    return (raw / 326.8f) + 25.0f;
}

// 查找 IIO 设备路径
int find_iio_device(const char *dev_name, char *dev_path, size_t size)
{
    int i;
    char path[MAX_PATH_LEN], name[64];
    for (i = 0; i < 16; i++)
    {
        snprintf(path, sizeof(path), "%s/iio:device%d/name", IIO_PATH, i);
        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;
        if (!fgets(name, sizeof(name), fp))
        {
            fclose(fp);
            continue;
        }
        fclose(fp);
        name[strcspn(name, "\r\n")] = 0;
        if (strcmp(name, dev_name) == 0)
        {
            snprintf(dev_path, size, "%s/iio:device%d/", IIO_PATH, i);
            return 0;
        }
    }
    return -1;
}

// 读取 LED/KEY
int read_sensor_dev(int *led, int *key)
{
    int fd = open(SENSOR_DEV, O_RDONLY);
    if (fd < 0)
        return -1;
    char buf[2] = {0};
    if (read(fd, buf, 2) <= 0)
    {
        close(fd);
        return -1;
    }
    close(fd);
    *led = buf[0];
    *key = buf[1];
    return 0;
}

int main()
{
    char icm_path[MAX_PATH_LEN] = {0};
    char ap_path[MAX_PATH_LEN] = {0};
    signal(SIGINT, handle_sigint);

    // 等待设备初始化
    while (find_iio_device("icm20608", icm_path, sizeof(icm_path)) < 0 ||
           find_iio_device("ap3216c", ap_path, sizeof(ap_path)) < 0)
    {
        printf("等待 ICM20608 或 AP3216C 驱动初始化...\n");
        usleep(100000);
    }
    printf("Found ICM20608: %s\n", icm_path);
    printf("Found AP3216C: %s\n", ap_path);

    while (!stop)
    {
        int i;

        // ICM20608
        int icm_vals[ICM_NUM_CHANNELS];
        for (i = 0; i < ICM_NUM_CHANNELS; i++)
        {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s", icm_path, icm_channels[i]);
            int val = read_sysfs_int(path);
            icm_vals[i] = (val < 0) ? 0 : val;
        }

        printf("ICM20608: Accel X=%.2fg Y=%.2fg Z=%.2fg | ",
               conv_accel(icm_vals[0]), conv_accel(icm_vals[1]), conv_accel(icm_vals[2]));
        printf("Gyro X=%.2f°/s Y=%.2f°/s Z=%.2f°/s | Temp=%.2f°C\n",
               conv_gyro(icm_vals[3]), conv_gyro(icm_vals[4]), conv_gyro(icm_vals[5]), conv_temp(icm_vals[6]));

        // AP3216C
        int ap_raw[AP_NUM_CHANNELS], ap_scale[AP_NUM_CHANNELS];
        for (i = 0; i < AP_NUM_CHANNELS; i++)
        {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s", ap_path, ap_channels_raw[i]);
            int val = read_sysfs_int(path);
            ap_raw[i] = (val < 0) ? 0 : val;

            snprintf(path, sizeof(path), "%s%s", ap_path, ap_channels_scale[i]);
            val = read_sysfs_int(path);
            ap_scale[i] = (val <= 0) ? 1 : val;
        }
        printf("AP3216C: ALS=%d lux, PS=%d, IR=%d\n",
               ap_raw[0] * ap_scale[0], ap_raw[1] * ap_scale[1], ap_raw[2] * ap_scale[2]);

        // LED / KEY
        int led = 0, key = 0;
        if (read_sensor_dev(&led, &key) == 0)
            printf("LED=%d KEY=%d\n", led, key);
        else
            printf("LED/KEY: 读取失败\n");

        printf("-------------------------------------------------\n");
        sleep(1);
    }

    printf("\nExiting...\n");
    return 0;
}