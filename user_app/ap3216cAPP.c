/**FileHeader
 * @Author: Farewellove
 * @Date: 2026/4/7 20:20:07
 * @LastEditors: Farewellove
 * @LastEditTime: 2026/5/8 16:05:48
 * @Description:
 * @Copyright: Copyright (©)}) 2026 Farewellove. All rights reserved.
 * @Email: 183085452@qq.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IIO_PATH "/sys/bus/iio/devices"
#define MAX_PATH_LEN 256

static int read_string_file(const char *path, char *buf, size_t size)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return -1;
    }

    if (!fgets(buf, size, fp))
    {
        fclose(fp);
        return -1;
    }

    buf[strcspn(buf, "\r\n")] = '\0';
    fclose(fp);
    return 0;
}

static int read_int_file(const char *path, int *value)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return -1;
    }

    if (fscanf(fp, "%d", value) != 1)
    {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int find_ap3216c_device(char *dev_path, size_t size)
{
    int i;

    for (i = 0; i < 16; i++)
    {
        char path[MAX_PATH_LEN];
        char name[64];

        snprintf(path, sizeof(path), "%s/iio:device%d/name", IIO_PATH, i);

        if (read_string_file(path, name, sizeof(name)) == 0)
        {
            if (strcmp(name, "ap3216c") == 0)
            {
                snprintf(dev_path, size, "%s/iio:device%d", IIO_PATH, i);
                return 0;
            }
        }
    }

    return -1;
}

static int read_channel(const char *dev_path,
                        const char *raw_name,
                        const char *scale_name,
                        int *raw,
                        int *scale)
{
    char raw_path[MAX_PATH_LEN];
    char scale_path[MAX_PATH_LEN];

    snprintf(raw_path, sizeof(raw_path), "%s/%s", dev_path, raw_name);
    snprintf(scale_path, sizeof(scale_path), "%s/%s", dev_path, scale_name);

    if (read_int_file(raw_path, raw) < 0)
    {
        printf("read failed: %s\n", raw_path);
        return -1;
    }

    if (read_int_file(scale_path, scale) < 0)
    {
        printf("read failed: %s\n", scale_path);
        return -1;
    }

    return 0;
}

int main(void)
{
    char dev_path[MAX_PATH_LEN];

    if (find_ap3216c_device(dev_path, sizeof(dev_path)) < 0)
    {
        printf("ap3216c iio device not found\n");
        return -1;
    }

    printf("Found AP3216C IIO device: %s\n", dev_path);

    while (1)
    {
        int als_raw = 0, als_scale = 0;
        int ps_raw = 0, ps_scale = 0;
        int ir_raw = 0, ir_scale = 0;

        read_channel(dev_path,
                     "in_illuminance_raw",
                     "in_illuminance_scale",
                     &als_raw,
                     &als_scale);

        read_channel(dev_path,
                     "in_proximity_raw",
                     "in_proximity_scale",
                     &ps_raw,
                     &ps_scale);

        read_channel(dev_path,
                     "in_intensity_ir_raw",
                     "in_intensity_ir_scale",
                     &ir_raw,
                     &ir_scale);

        printf("ALS: raw=%d, scale=%d, value=%d lux | "
               "PS: raw=%d, scale=%d, value=%d | "
               "IR: raw=%d, scale=%d, value=%d\n",
               als_raw, als_scale, als_raw * als_scale,
               ps_raw, ps_scale, ps_raw * ps_scale,
               ir_raw, ir_scale, ir_raw * ir_scale);

        sleep(1);
    }

    return 0;
}