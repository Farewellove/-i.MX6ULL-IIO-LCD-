/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/5/8 16:23:27
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/13 21:36:57
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define IIO_PATH "/sys/bus/iio/devices"
#define MAX_PATH_LEN 256

const char *ap_channels_raw[] = {
    "in_illuminance_raw",
    "in_proximity_raw",
    "in_intensity_ir_raw"};
const char *ap_channels_scale[] = {
    "in_illuminance_scale",
    "in_proximity_scale",
    "in_intensity_ir_scale"};
#define AP_NUM_CHANNELS (sizeof(ap_channels_raw) / sizeof(ap_channels_raw[0]))

volatile sig_atomic_t stop = 0;
void handle_sigint(int sig) { stop = 1; }

// 读取 sysfs 整数值
int read_sysfs_int(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return -1;
    int val = 0;
    if (fscanf(fp, "%d", &val) != 1)
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return val;
}

// 动态查找 AP3216C iio device
int find_ap3216c(char *dev_path, size_t size)
{
    int i;
    for (i = 0; i < 16; i++)
    {
        char path[MAX_PATH_LEN];
        char name[64];
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
        if (strcmp(name, "ap3216c") == 0)
        {
            snprintf(dev_path, size, "%s/iio:device%d/", IIO_PATH, i);
            return 0;
        }
    }
    return -1;
}

int main()
{
    char dev_path[MAX_PATH_LEN];
    // 等待设备节点出现
    while (find_ap3216c(dev_path, sizeof(dev_path)) < 0)
    {
        printf("Waiting for AP3216C sysfs node...\n");
        sleep(1);
    }
    printf("Found AP3216C IIO device: %s\n", dev_path);

    signal(SIGINT, handle_sigint);

    while (!stop)
    {
        int i;
        int raw[AP_NUM_CHANNELS], scale[AP_NUM_CHANNELS];

        for (i = 0; i < AP_NUM_CHANNELS; i++)
        {
            char path[MAX_PATH_LEN];

            snprintf(path, sizeof(path), "%s%s", dev_path, ap_channels_raw[i]);
            int val = read_sysfs_int(path);
            raw[i] = (val < 0) ? 0 : val;

            snprintf(path, sizeof(path), "%s%s", dev_path, ap_channels_scale[i]);
            val = read_sysfs_int(path);
            scale[i] = (val <= 0) ? 1 : val;
        }

        printf("AP3216C: ALS=%d lux, PS=%d, IR=%d\n",
               raw[0] * scale[0], raw[1] * scale[1], raw[2] * scale[2]);
        sleep(1);
    }

    printf("\nExiting...\n");
    return 0;
}