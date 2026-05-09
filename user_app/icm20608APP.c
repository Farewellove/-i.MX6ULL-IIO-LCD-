#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEVICE_PATH "/sys/bus/iio/devices/iio:device0/"

#define ACCEL_X_RAW DEVICE_PATH "in_accel_x_raw"
#define ACCEL_Y_RAW DEVICE_PATH "in_accel_y_raw"
#define ACCEL_Z_RAW DEVICE_PATH "in_accel_z_raw"

#define GYRO_X_RAW DEVICE_PATH "in_anglvel_x_raw"
#define GYRO_Y_RAW DEVICE_PATH "in_anglvel_y_raw"
#define GYRO_Z_RAW DEVICE_PATH "in_anglvel_z_raw"

#define TEMP_RAW DEVICE_PATH "in_temp_raw"

int read_int_from_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        perror(path);
        return 0;
    }
    int value = 0;
    if (fscanf(fp, "%d", &value) != 1)
    {
        perror("fscanf failed");
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return value;
}

int main(void)
{
    while (1)
    {
        int ax = read_int_from_file(ACCEL_X_RAW);
        int ay = read_int_from_file(ACCEL_Y_RAW);
        int az = read_int_from_file(ACCEL_Z_RAW);

        int gx = read_int_from_file(GYRO_X_RAW);
        int gy = read_int_from_file(GYRO_Y_RAW);
        int gz = read_int_from_file(GYRO_Z_RAW);

        int temp = read_int_from_file(TEMP_RAW);

        printf("Accel: X=%d Y=%d Z=%d | Gyro: X=%d Y=%d Z=%d | Temp=%d\n",
               ax, ay, az, gx, gy, gz, temp);

        usleep(100000); // 0.1秒
    }
    return 0;
}