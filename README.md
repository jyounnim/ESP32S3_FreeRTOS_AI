# ESP32-S3 AI Lab Series (FreeRTOS/ESP-IDF Based)

A hands-on lab series for on-device AI on the ESP32-S3 using LiteRT for Microcontrollers (TFLM, formerly TensorFlow Lite Micro), built on **FreeRTOS via ESP-IDF** (PlatformIO's `framework = espidf`). Written for lecture/seminar use, and every claim in these docs has been verified on real hardware unless noted otherwise.

> This repository is the **FreeRTOS/ESP-IDF track**. A separate, independent lab series covering the same topic (AI on ESP32-S3) exists using **Zephyr RTOS**, but the two tracks live in completely separate repositories/folders and neither assumes the other. You can start with either one, and completing just one track is enough on its own. Every doc and every line of code in this repository is written purely in terms of FreeRTOS/ESP-IDF.

## Layout of this track

```
05_AI/
├── 00_LITERT_TFLM_SETUP/     Lab 0 — development environment setup (required first)
├── 01_SENSOR_ANOMALY_LAB/    Lab 1 — anomaly detection with an LIS3DH accelerometer (Autoencoder)
└── 02_WAKE_WORD_LAB/         Lab 2 — wake-word detection with an INMP441 microphone
```

Each lab folder shares the same structure:

```
NN_LAB_NAME/
├── doc/    Korean (*_KR.md) / English (*_EN.md) documentation
└── lab/    Lab source code (a PlatformIO project)
```

## Lab order — 00 is a prerequisite, 01/02 are independent of each other

- **Always do `00_LITERT_TFLM_SETUP` first.** Every later lab reuses the development environment this lab verifies (PlatformIO's `framework = espidf`, the `esp-tflite-micro` component, and so on).
- Once lab 00 is done, **`01_SENSOR_ANOMALY_LAB` and `02_WAKE_WORD_LAB` are fully independent of each other.** Do them in any order, or do just one of them — it doesn't matter. Each lab's doc never assumes you've already done another lab; every piece of context it needs is included in that lab's own doc.
- Each lab needs different sensor/microphone hardware. Use the table below to pick the lab that matches the hardware you have on hand.

## Lab summary and verification status

| Lab | Topic | Extra hardware needed (beyond the common board) | Status |
| --- | --- | --- | --- |
| [`00_LITERT_TFLM_SETUP`](00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_EN.md) | ESP-IDF + esp-tflite-micro environment setup, verified with Hello World | None (board only) | ✅ Verified on real hardware |
| [`01_SENSOR_ANOMALY_LAB`](01_SENSOR_ANOMALY_LAB/doc/01_SENSOR_ANOMALY_LAB_EN.md) | Autoencoder-based anomaly detection on an HW-664 (LIS3DH) accelerometer, with an optional SSD1306 status display | HW-664 (LIS3DH), optional SSD1306 OLED | ✅ Verified on real hardware |
| [`02_WAKE_WORD_LAB`](02_WAKE_WORD_LAB/doc/02_WAKE_WORD_LAB_KR.md) | Wake-word detection with an INMP441 I2S microphone (Step 1: the `micro_speech` yes/no demo, Step 2: a custom wake word via Edge Impulse) | INMP441 (or a compatible I2S microphone) | ⚠️ Docs and code complete, not yet verified on real hardware |

Common board: every lab is written and, where verified, tested against the **ESP32-S3-DevKitC-1 N16R8** (16 MB Quad Flash, 8 MB Octal PSRAM).

## Getting started

1. **Hardware**: get an ESP32-S3-DevKitC-1 N16R8 board and a USB cable that supports data (not charge-only). See the table above for any extra hardware a specific lab needs.
2. **Environment setup**: follow [the `00_LITERT_TFLM_SETUP` doc](00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_EN.md) to set up VS Code + PlatformIO (`framework = espidf`), and verify this lab itself on real hardware.
3. **Pick a lab**: once lab 00 is done, open the doc in the `doc/` folder of whichever lab matches your hardware (01 or 02) and follow it. Each doc covers everything you need: parts list, wiring, build/upload steps, a verification checklist, and troubleshooting.
4. Each lab's `lab/` folder holds the actual PlatformIO project source. Every code comment is written in English.

## About the doc languages

- Korean docs: `*_KR.md`
- English docs: `*_EN.md` — published only once that lab has been fully verified on real hardware. (`02_WAKE_WORD_LAB` currently has only a Korean doc, since it hasn't been verified on real hardware yet.)

## About build artifacts

Every lab's `.gitignore` excludes `.pio/`, `build/`, `managed_components/`, and `sdkconfig*` (which also covers the board-specific `sdkconfig.<board>` variant PlatformIO generates). `managed_components/` holds dependencies (`esp-tflite-micro`, `esp-nn`, and so on) that PlatformIO's IDF Component Manager fetches automatically from the internet on build, so they aren't checked into this repository — your first build repopulates them.
