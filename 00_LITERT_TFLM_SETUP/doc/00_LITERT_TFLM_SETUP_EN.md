# 00. LiteRT (TFLM) Development Environment Setup — ESP32-S3

## Goal of This Lab

Build a development environment from scratch for running on-device AI inference (TinyML) on the ESP32-S3-DevKitC-1 (N16R8), and confirm that a very small example model (a sine-function approximation) actually runs inference successfully on the board. Once this lab is done, labs 01 (face detection), 02 (wake word), and 03 (sensor anomaly detection) can all be built on top of the same environment.

Google rebranded TensorFlow Lite as **LiteRT**, but the runtime for microcontrollers is still called **TFLM (TensorFlow Lite Micro / LiteRT for Microcontrollers)**. On the ESP32-S3 we use **`esp-tflite-micro`**, Espressif's official port — it includes `esp-nn`, an accelerated kernel set that takes advantage of the S3's vector instructions (SIMD), so it runs inference faster than plain TFLM.

## What You'll Need

- ESP32-S3-DevKitC-1 (N16R8: 16MB Quad Flash + 8MB Octal PSRAM) — the same board used in earlier labs
- A USB cable and a PC (assuming VS Code + PlatformIO are already installed)
- A camera/microphone is **not** needed for this lab (00) — they're only needed in labs 01/02.

## ⚠️ Read This First — A Framework Change

Every GPIO/Wi-Fi/MQTT/OTA/FreeRTOS lab up to this point used `framework = arduino`. However, `esp-tflite-micro` is distributed as an **ESP-IDF-only component**, and forcing it into PlatformIO's Arduino framework commonly produces compile errors such as a missing `array.h` (this is a confirmed issue on Espressif's own GitHub tracker).

**The fix**: for these AI labs specifically, create a new PlatformIO project and use `framework = espidf`. The development environment itself — VS Code + PlatformIO — stays the same; only the underlying build system switches to plain ESP-IDF. This follows a common embedded-development rule of thumb: a given component ecosystem is most reliable when built with the build system that ecosystem officially supports.

## Step 1. Create a New Project (ESP-IDF Framework)

1. PlatformIO Home → New Project
2. Name: `00_LITERT_TFLM_SETUP` (or whatever you like)
3. Board: select `Espressif ESP32-S3-DevKitC-1`
4. **Framework: select `Espressif IoT Development Framework`** (not Arduino — double-check this)
5. After creation, confirm the project root's `platformio.ini` looks like this:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 115200
```

This lab's `lab/platformio.ini` already has the same content prepared, so you can just overwrite your newly created project's file with it.

## Step 2. Configure N16R8 Flash/PSRAM (the ESP-IDF Way)

The earlier Arduino-framework projects configured Flash/PSRAM with `board_build.*` options, but an ESP-IDF project configures them through a **`sdkconfig.defaults` file**. Unlike Arduino, ESP-IDF builds directly from source, so you have complete freedom to customize `sdkconfig`.

Create a `sdkconfig.defaults` file at the project root and add the following (this lab's `lab/sdkconfig.defaults` already has it prepared):

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
```

These values are tuned for the N16R8 (16MB Quad Flash + 8MB Octal PSRAM) — the same thing the `qio_opi` configuration (Quad flash, Octal PSRAM) expressed in the earlier labs, just written the ESP-IDF way. TFLM inference uses a fair amount of memory for the model and the tensor arena, so making sure PSRAM is actually recognized matters especially here.

## Step 3. Add the esp-tflite-micro Dependency (IDF Component Manager)

ESP-IDF declares external components through a file called `idf_component.yml`. Create `src/idf_component.yml` (this lab's `lab/src/idf_component.yml` already has it prepared).

> **PlatformIO uses `src/`, not `main/`**: plain ESP-IDF (`idf.py`) always names the application component folder `main`, but PlatformIO defaults to a `src/` folder instead, to stay consistent with its other frameworks (Arduino, etc.), unless you override `src_dir` in `platformio.ini`. This lab's `lab/` folder is already set up around `src/`.

```yaml
dependencies:
  espressif/esp-tflite-micro: "^1.3.3"
```

When you build in PlatformIO, the IDF Component Manager automatically fetches this dependency from the internet (downloaded into a `managed_components/` folder). Since this folder gets repopulated on every build, it's typically excluded from version control (git).

## Step 4. Verify the Environment with Hello World

`esp-tflite-micro` ships a `hello_world` example that checks whether the environment is set up correctly using a tiny model that approximates a sine function. Rather than retyping the code by hand, it's recommended to fetch the example itself with the commands below and build it as-is.

```bash
# Build once first, to fetch managed_components
pio run

# Copy the example from managed_components into the project's src/ folder
# (inside the example's own repo the folder is named "main", but we bring
# it into "src" for our project)
cp -r managed_components/espressif__esp-tflite-micro/examples/hello_world/main/* src/

# Build & upload again
pio run -t upload
pio device monitor
```

Once built, if the serial monitor prints a sine-wave prediction every 500ms, your environment setup is complete.

```
x_value: 1.1, y_value: 1.0
x_value: 1.2, y_value: 0.9
...
```

## Verification Checklist

- [ ] Created the PlatformIO project with `framework = espidf`
- [ ] Applied the N16R8 Flash/PSRAM settings in `sdkconfig.defaults`
- [ ] Added the `espressif/esp-tflite-micro` dependency in `src/idf_component.yml`
- [ ] Built/uploaded the `hello_world` example successfully and confirmed sine-wave output

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `Failed to resolve component 'tflite-lib'` | The location or syntax of `idf_component.yml` is wrong — check that it's exactly inside the `src/` folder (PlatformIO uses `src/`, not `main/`) |
| PSRAM isn't recognized | Check whether `sdkconfig.defaults` was actually applied — open `.pio/build/esp32-s3-devkitc-1/sdkconfig` and check for `CONFIG_SPIRAM=y`. If it's missing, run `pio run -t clean` and rebuild |
| `array.h`-related compile error | Check that you aren't accidentally trying this with the Arduino framework — it must be `framework = espidf` |
| `idf_component.yml` dependency download fails (network error) | Check your PC's internet connection/firewall. On a corporate network, you may need proxy settings |

## What's next

Once your environment is set up, the remaining two labs in this track are independent of each other, so do them in whichever order you like.

1. `01_SENSOR_ANOMALY_LAB` — vibration/sensor anomaly detection (requires an HW-664 module/LIS3DH 3-axis accelerometer; an SSD1306 OLED is optional)
2. `02_WAKE_WORD_LAB` — wake word detection (requires an I2S microphone)
