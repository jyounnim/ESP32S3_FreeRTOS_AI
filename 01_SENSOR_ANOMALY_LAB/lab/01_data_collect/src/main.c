/*
 * 01_SENSOR_ANOMALY_LAB - Step 1: Normal-state data collection
 *
 * Reads the HW-664 module (LIS3DH accelerometer, I2C) and prints
 * SAMPLE_WINDOW consecutive (x,y,z) readings as one comma-separated
 * "window" per line. Redirect the serial monitor output to a CSV file
 * to build the training set for Step 2 (Autoencoder training on PC).
 *
 * Run this while moving the sensor through its NORMAL range of motion
 * only - no need to create "abnormal" samples, the Autoencoder only
 * needs to learn what "normal" looks like.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "oled_status.h"

// I2C0 pins - same convention as this series' Zephyr I2C0 labs (SDA=GPIO8, SCL=GPIO9)
#define I2C_SDA_GPIO       GPIO_NUM_8
#define I2C_SCL_GPIO       GPIO_NUM_9
#define I2C_CLK_SPEED_HZ   400000

// HW-664 module = LIS3DH accelerometer. Address confirmed as 0x19 in this
// series' earlier Zephyr sensor labs (SDO/SA0 pulled high). If your module
// has SDO/SA0 tied to GND instead, the address is 0x18 - see the KR doc's
// troubleshooting table.
#define LIS3DH_ADDR            0x19
#define LIS3DH_REG_WHO_AM_I    0x0F
#define LIS3DH_REG_CTRL_REG1   0x20
#define LIS3DH_REG_OUT_X_L     0x28
#define LIS3DH_WHO_AM_I_VALUE  0x33

#define SAMPLE_WINDOW      20   // 20 (x,y,z) readings grouped into one "pattern"
#define SAMPLE_PERIOD_MS   20   // 20ms * 20 samples = 400ms window

static const char *TAG = "DATA_COLLECT";

static i2c_master_dev_handle_t s_lis3dh;

static void lis3dh_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    ESP_ERROR_CHECK(i2c_master_transmit(s_lis3dh, buf, sizeof(buf), -1));
}

static uint8_t lis3dh_read_reg(uint8_t reg)
{
    uint8_t val = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(s_lis3dh, &reg, 1, &val, 1, -1));
    return val;
}

// Reads X/Y/Z in a single auto-incrementing transaction (setting the MSB of
// the register address enables auto-increment on ST parts) and returns raw
// 16-bit signed counts - no unit conversion, just what the ADC-style anomaly
// pipeline needs.
static void lis3dh_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t reg = LIS3DH_REG_OUT_X_L | 0x80;
    uint8_t data[6];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(s_lis3dh, &reg, 1, data, sizeof(data), -1));
    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LIS3DH_ADDR,
        .scl_speed_hz = I2C_CLK_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_lis3dh));

    uint8_t who_am_i = lis3dh_read_reg(LIS3DH_REG_WHO_AM_I);
    if (who_am_i != LIS3DH_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X (expected 0x%02X) - check wiring/address",
                 who_am_i, LIS3DH_WHO_AM_I_VALUE);
    } else {
        ESP_LOGI(TAG, "LIS3DH detected (WHO_AM_I = 0x%02X)", who_am_i);
    }

    // CTRL_REG1: ODR=100Hz (0101), LPen=0 (normal power mode), Zen=Yen=Xen=1
    lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x57);

    // Optional status OLED on I2C1 (SDA=GPIO4, SCL=GPIO5) - purely cosmetic,
    // the data-collection loop below works the same with or without it.
    oled_status_init();
    oled_status_show("LAB01 Step1", "Collecting...", "windows: 0");

    uint32_t window_count = 0;

    while (1) {
        for (int i = 0; i < SAMPLE_WINDOW; i++) {
            int16_t x, y, z;
            lis3dh_read_xyz(&x, &y, &z);
            printf("%d,%d,%d", x, y, z);
            if (i < SAMPLE_WINDOW - 1) {
                printf(",");
            }
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        }
        printf("\n");

        window_count++;
        char line2[17];
        snprintf(line2, sizeof(line2), "windows: %lu", (unsigned long)window_count);
        oled_status_show("LAB01 Step1", "Collecting...", line2);
    }
}
