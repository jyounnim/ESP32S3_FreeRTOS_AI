/*
 * 01_SENSOR_ANOMALY_LAB - Step 4: On-device Autoencoder inference
 *
 * Reads the same LIS3DH (x,y,z) window as Step 1, runs it through the
 * INT8-quantized Autoencoder trained in Step 2, computes the reconstruction
 * error (MSE), and flags an anomaly when it exceeds ANOMALY_THRESHOLD.
 *
 * Before building:
 *   1. Replace src/anomaly_model.h with the one generated in Step 3.
 *   2. Replace ANOMALY_THRESHOLD below with the value train_autoencoder.py
 *      printed in Step 2.
 */

#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "anomaly_model.h"
#include "oled_status.h"

// I2C0 pins - same convention as this series' Zephyr I2C0 labs (SDA=GPIO8, SCL=GPIO9)
#define I2C_SDA_GPIO       GPIO_NUM_8
#define I2C_SCL_GPIO       GPIO_NUM_9
#define I2C_CLK_SPEED_HZ   400000

// HW-664 module = LIS3DH accelerometer, address 0x19 (see Step 1 / KR doc troubleshooting)
#define LIS3DH_ADDR            0x19
#define LIS3DH_REG_WHO_AM_I    0x0F
#define LIS3DH_REG_CTRL_REG1   0x20
#define LIS3DH_REG_OUT_X_L     0x28
#define LIS3DH_WHO_AM_I_VALUE  0x33

#define SAMPLE_WINDOW 20           // must match Step 1 / Step 2
#define AXES 3                     // X, Y, Z
#define FEATURE_LEN (SAMPLE_WINDOW * AXES)  // 60
#define SAMPLE_PERIOD_MS 20
#define RAW_FULL_SCALE 32768.0f    // must match Step 2's normalization
#define ANOMALY_THRESHOLD 0.02f    // <-- replace with the value from Step 2

static const char *TAG = "INFERENCE";

constexpr int kTensorArenaSize = 16 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];

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

static void lis3dh_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t reg = LIS3DH_REG_OUT_X_L | 0x80;
    uint8_t data[6];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(s_lis3dh, &reg, 1, data, sizeof(data), -1));
    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
}

extern "C" void app_main(void)
{
    // --- I2C / LIS3DH init (same as Step 1) ---
    // Field order below must match i2c_master_bus_config_t's declaration
    // order (i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt,
    // ..., flags) - C++20 designated initializers require this, unlike C.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
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
    }
    lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x57);  // 100Hz, normal power, X/Y/Z enabled

    // --- TFLM init ---
    const tflite::Model* model = tflite::GetModel(anomaly_model_tflite);

    // Only the ops this specific model actually uses are registered, to keep
    // the binary small - see the KR doc's "Observation points" section.
    // Decoder output activation is Tanh (not Logistic/sigmoid), because
    // normalized accelerometer values can be negative.
    static tflite::MicroMutableOpResolver<3> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddTanh();

    static tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        MicroPrintf("AllocateTensors() failed - increase kTensorArenaSize");
        return;
    }

    TfLiteTensor* input = interpreter.input(0);
    TfLiteTensor* output = interpreter.output(0);

    // Optional status OLED on I2C1 (SDA=GPIO4, SCL=GPIO5) - purely cosmetic,
    // the inference loop below works the same with or without it.
    oled_status_init();
    oled_status_show("LAB01 Step4", "Starting...", "");

    while (1) {
        float window[FEATURE_LEN];
        for (int i = 0; i < SAMPLE_WINDOW; i++) {
            int16_t x, y, z;
            lis3dh_read_xyz(&x, &y, &z);
            window[i * AXES + 0] = x / RAW_FULL_SCALE;
            window[i * AXES + 1] = y / RAW_FULL_SCALE;
            window[i * AXES + 2] = z / RAW_FULL_SCALE;
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        }

        // Fill the input tensor (applying the model's quantization scale)
        for (int i = 0; i < FEATURE_LEN; i++) {
            input->data.int8[i] =
                (int8_t)(window[i] / input->params.scale + input->params.zero_point);
        }

        interpreter.Invoke();

        // Reconstruction error (MSE) between input and decoder output
        float mse = 0.0f;
        for (int i = 0; i < FEATURE_LEN; i++) {
            float reconstructed =
                (output->data.int8[i] - output->params.zero_point) * output->params.scale;
            float diff = window[i] - reconstructed;
            mse += diff * diff;
        }
        mse /= FEATURE_LEN;

        char mse_line[17];
        snprintf(mse_line, sizeof(mse_line), "mse=%.4f", mse);

        if (mse > ANOMALY_THRESHOLD) {
            printf("ANOMALY DETECTED! mse=%.4f\n", mse);
            oled_status_show("LAB01 Step4", "** ANOMALY **", mse_line);
        } else {
            printf("normal, mse=%.4f\n", mse);
            oled_status_show("LAB01 Step4", "normal", mse_line);
        }
    }
}
