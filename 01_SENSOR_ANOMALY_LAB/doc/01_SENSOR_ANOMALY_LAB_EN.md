# 01. Vibration/Sensor Anomaly Detection

## Goal of This Lab

Train an **Autoencoder** model on normal-state sensor readings only, deploy it to the ESP32-S3, and have it flag "anomaly" in real time whenever a different-from-usual pattern shows up. You will build the full on-device AI pipeline yourself, end to end: data collection (board) → model training (PC) → quantization/conversion (PC) → inference (board) — the four typical stages of a TinyML project.

This lab assumes the ESP-IDF development environment set up in [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_EN.md) is already in place. If you haven't done that yet, please complete lab 00 first.

## What You'll Need

- ESP32-S3-DevKitC-1 (N16R8)
- **HW-664 module (LIS3DH 3-axis accelerometer, I2C)** x1
- **SSD1306 OLED module (128x64, I2C)** x1 — optional. It shows which stage (data collection / inference) is currently running and the live status (normal/anomaly) on screen. Everything is also visible over the serial monitor without it, so it isn't required for the lab itself.
- Python 3.9+ and TensorFlow installed on your PC (model training runs on the **PC**, not the board)

### Wiring (I2C0 + I2C1, two buses used simultaneously)

The LIS3DH and the SSD1306 go on **two separate I2C buses** — the ESP32-S3 has two independent I2C controllers (I2C0, I2C1) you can use at the same time. The pins below were chosen to avoid the strapping pins and the USB-JTAG pins, so they're safe to use.

| Module | Pin | ESP32-S3 GPIO | I2C bus |
|---|---|---|---|
| HW-664 (LIS3DH) | SDA | GPIO8 | I2C0 |
| HW-664 (LIS3DH) | SCL | GPIO9 | I2C0 |
| SSD1306 (OLED) | SDA | GPIO4 | I2C1 |
| SSD1306 (OLED) | SCL | GPIO5 | I2C1 |
| Common | VCC | 3.3V | — |
| Common | GND | GND | — |

> The HW-664 module is a **LIS3DH** chip (some vendor listings mislabel it as LIS3DSH — if the `WHO_AM_I` register reads `0x33`, it's a LIS3DH; a LIS3DSH would return a different value). The I2C address has been confirmed as **0x19** on this module (with the SDO/SA0 pin pulled high) — if you're using a different unit and it doesn't respond, it may be `0x18` depending on the SDO pin's state; see the troubleshooting table below.
>
> The SSD1306's I2C address varies by module (0x3C or 0x3D), so the code auto-probes both addresses in order at boot — just wire it up, no extra configuration needed.

## Status OLED (Optional)

Both `lab/01_data_collect` and `lab/03_inference` include a very small SSD1306 driver at `src/oled_status.c`/`oled_status.h` (128x64, I2C1, 8x8 text). ESP-IDF doesn't ship a standard display driver stack out of the box, so only the minimum this lab needs (clear the screen + print up to 3 lines of text) was implemented from scratch.

- `oled_status_init()`: call once at boot — if the SSD1306 is missing or doesn't respond, it **silently disables itself without an error** and the rest of the program runs normally (i.e. it's not required hardware).
- `oled_status_show(title, line1, line2)`: clears the screen and shows 3 lines. Each line holds up to 16 characters (anything beyond that is truncated).
- **Step 1 (`01_data_collect`)**: line 1 shows `LAB01 Step1`, line 2 shows `Collecting...`, and line 3 updates live with the number of windows collected so far (`windows: N`).
- **Step 4 (`03_inference`)**: line 1 shows `LAB01 Step4`, line 2 shows `normal` or `** ANOMALY **`, and line 3 updates every inference with the just-computed reconstruction error (`mse=0.0123`) — you can tell at a glance whether it's normal or anomalous right now, just from the screen.

If you'd like to reuse this display in another lab (02), just copy the three files `oled_status.h`/`oled_status.c`/`font8x8_basic.h` as-is (assuming the I2C1/GPIO4·5 wiring is already in place).

## Concept — Autoencoder-Based Anomaly Detection

An **Autoencoder** is a neural network trained to compress its input (encoder) and then reconstruct it (decoder) as faithfully as possible. If you train it on "normal" data only, it reconstructs normal patterns well (small reconstruction error), but struggles with abnormal patterns it never saw during training (large reconstruction error). When this reconstruction error crosses some threshold, we call it an "anomaly."

The key advantage of this approach is that **you never need to collect abnormal data separately** — normal data alone is enough to train it. In the real world, collecting "failure cases" ahead of time is often difficult in itself (failures don't happen often, and their causes vary widely), which is exactly why this approach is so practical.

The overall flow looks like this:

```
[Board] Read X/Y/Z acceleration from LIS3DH -> output as CSV over serial
   |
[PC]    Load CSV -> train Autoencoder -> derive threshold from the normal-data
        reconstruction-error distribution
   |
[PC]    Quantize the model to INT8 -> .tflite -> convert to a C array (.h)
   |
[Board] Include the .h file in the project -> run real-time inference with TFLM
        -> compute reconstruction error -> compare against the threshold
```

## Step 1. Data Collection (ESP-IDF, Reading the LIS3DH over I2C)

Open the PlatformIO ESP-IDF project already set up in `lab/01_data_collect/`. The source lives in `src/main.c` (PlatformIO uses `src/` as its default source folder instead of vanilla ESP-IDF's `main/` — see the lab 00 doc).

The core logic in `src/main.c`:

```c
#define SAMPLE_WINDOW 20   // group 20 (x,y,z) samples into one "pattern"

// CTRL_REG1: ODR=100Hz, normal power mode, X/Y/Z axes all enabled
lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x57);

while (1) {
    for (int i = 0; i < SAMPLE_WINDOW; i++) {
        int16_t x, y, z;
        lis3dh_read_xyz(&x, &y, &z);           // auto-incrementing 6-byte read from OUT_X_L
        printf("%d,%d,%d", x, y, z);
        if (i < SAMPLE_WINDOW - 1) printf(",");
        vTaskDelay(pdMS_TO_TICKS(20));         // 20ms interval
    }
    printf("\n");
}
```

The code already initializes the bus and device with the new I2C master driver (`driver/i2c_master.h`, ESP-IDF v5.x's standard API), and checks right after boot that the `WHO_AM_I` register (0x0F) reads `0x33` to verify the wiring/address is correct. If an SSD1306 is connected (I2C1), the screen shows "Collecting..." and a live window count right after boot — see the "Status OLED" section above for details.

After building/uploading, save the serial log to a text file on your PC.

```bash
cd 01_data_collect
pio run -t upload
pio device monitor > normal_data.csv
```

Collect at least a few hundred to a few thousand lines of **normal-state** data — move the board the way you'd normally handle it, slowly and regularly, or leave it sitting on top of a normal vibration source you'd encounter in the real deployment environment (e.g. on top of a fan, next to a motor) while it records. At this point there's no need to create "abnormal" data separately (see the Autoencoder concept section). Once you've collected enough, stop the monitor with `Ctrl+C`, and move the resulting `normal_data.csv` into the `lab/02_train_autoencoder/` folder.

> **Important**: Don't collect "normal" data only while the board is sitting perfectly still on a desk. An Autoencoder only learns as "normal" whatever patterns actually appear in the training data — so if the data is too quiet/static, the standard deviation of the reconstruction error ends up close to zero, which in turn makes the threshold computed in Step 2 (mean + 3×std) unrealistically small. That means even the ordinary variation from ordinary handling — a slight hand tremor, a small shake — will exceed the threshold, and you'll end up with **constant ANOMALY readings**. This is a common gotcha in this lab, so when collecting normal data, deliberately include the kind of movement/vibration variation you'd actually expect in the deployment environment (see the "Constant ANOMALY readings" row in the troubleshooting table below too).

> **Note**: The first and last lines may not have exactly 60 values (SAMPLE_WINDOW×AXES). This is because the moment you start `pio device monitor` can land in the middle of the board printing a window (truncating the first line), and stopping with `Ctrl+C` can likewise land mid-print for the last window. Step 2's `train_autoencoder.py` automatically filters out such lines and reports how many it skipped, so **you don't need to manually delete them** — just leave them as they are.

## Step 2. Train the Autoencoder in Python (on the PC, independent of the board)

Run `lab/02_train_autoencoder/train_autoencoder.py`. First, install the dependencies.

> If your PC already has other Python projects installed, it's recommended to create a dedicated virtual environment (venv) just for this lab. That way you avoid the `pip` dependency-conflict warnings that come from version clashes with packages already installed for other projects (see the troubleshooting table below).
>
> ```bash
> cd 02_train_autoencoder
> python -m venv venv
> # Windows
> venv\Scripts\activate
> # macOS/Linux
> source venv/bin/activate
> ```

```bash
pip install -r requirements.txt
python train_autoencoder.py
```

The script does the following:

1. Reads `normal_data.csv` line by line, automatically skipping (and reporting the count of) any line whose value count isn't exactly 60 (SAMPLE_WINDOW×AXES) — usually just the first/last line — then normalizes to roughly -1..1 (raw 16-bit LIS3DH values ÷ 32768.0)
2. Trains a small 60 (=20 samples × 3 axes) → 16 → 60 Autoencoder for 50 epochs
3. Prints the normal-data reconstruction-error distribution and computes `mean + 3×std` as the recommended threshold
4. Quantizes the model to INT8 and saves it as `anomaly_model.tflite`

**Make sure to write down the recommended threshold value** printed in the output — you'll plug it directly into the code in Step 4.

## Step 3. Convert the Model to a C Array

```bash
xxd -i anomaly_model.tflite > anomaly_model.h
```

`xxd` ships by default on Linux/macOS/WSL. If you're on Windows without WSL, you can use the `xxd` bundled with Git Bash, or do the same conversion in Python.

```python
# Alternative to xxd -i, if it's not available
with open("anomaly_model.tflite", "rb") as f:
    data = f.read()
with open("anomaly_model.h", "w") as f:
    f.write("unsigned char anomaly_model_tflite[] = {\n")
    f.write(",".join(f"0x{b:02x}" for b in data))
    f.write("\n};\n")
    f.write(f"unsigned int anomaly_model_tflite_len = {len(data)};\n")
```

Drop the resulting `anomaly_model.h` into the `lab/03_inference/src/` folder (overwriting the existing placeholder file).

## Step 4. Run Inference on the ESP32-S3

In `lab/03_inference/`, replace the `ANOMALY_THRESHOLD` value in `src/main.cc` with the threshold you got from Step 2.

```c
#define ANOMALY_THRESHOLD 0.02f   // replace with the threshold value from Step 2
```

The I2C init / `lis3dh_read_xyz()` code reuses the exact same logic as Step 1. If an SSD1306 is connected, `normal` or `** ANOMALY **` along with the reconstruction-error value refreshes on screen with every inference.

```bash
cd 03_inference
pio run -t upload
pio device monitor
```

## Run & Verify

- Handle the board as usual and confirm `normal, mse=...` keeps printing.
- Create an out-of-the-ordinary pattern (suddenly shaking it hard, tapping it, standing it up in an unusual orientation, etc.) and confirm `ANOMALY DETECTED! mse=...` shows up.

## Observations

- This pipeline's input is a "20 samples × 3 axes = 60-dimensional vector." If you add more axes (e.g. a gyroscope) or increase the sample count, you only need to adjust the input dimension and the encoder/decoder size accordingly — the rest of the training/quantization/deployment flow stays the same.
- The threshold isn't something you set once and forget — you need to watch how much the normal-state error actually varies in the real deployment environment and adjust it. Too low and you get frequent false positives; too high and you miss real anomalies. In particular, if you collected the training data in a too-quiet state, the threshold itself comes out abnormally small and you'll keep getting false positives — that's not a code bug, it's **the training data having defined "normal" too narrowly**. Recollect the data (recommended), or as a quick fix, loosen the threshold multiplier (the `3` in `errors.mean() + 3 * errors.std()`) to something like 4-5.
- The reason only the operations actually needed are registered via `MicroMutableOpResolver<3>` is that TFLM including every possible op would eat up a lot of flash space — this model only uses FullyConnected + Relu (encoder) + Tanh (decoder output), so only those three are registered. The decoder's final activation was changed from sigmoid to **tanh** because, unlike a plain ADC reading, raw accelerometer values can be both negative and positive, so the normalized range is -1..1 (sigmoid only outputs 0..1).
- The LIS3DH can output data at up to 5.3kHz via `CTRL_REG1`'s output data rate (ODR) setting — 100Hz is plenty for this lab, but if you need to catch faster vibration (e.g. motor bearing defects), raise the ODR and shrink `SAMPLE_PERIOD_MS` to match.

## Extending This Lab Differently

**Switching to an SW-420/knock sensor (a digital vibration switch)**: this kind of sensor gives you "did an event happen" rather than a continuous acceleration value. Instead of `lis3dh_read_xyz()`, it's natural to **count** how many interrupts fired during the `SAMPLE_WINDOW` slots and turn that into a single value.

```c
// Instead of reading 3 axes per slot, use e.g. the event count over a
// 400ms window as the feature value
volatile int event_count = 0;   // incremented in the ISR
// ... at the end of the window, put event_count into window[i] and reset it to 0
```

In a normal state (ordinary vibration level), the count stays low and steady, while in an abnormal state (impact, failure, etc.) the count spikes — a pattern the Autoencoder learns just as before. In this case the input dimension becomes 1 (the count) instead of 3 axes, so just change `AXES` to `1` in `train_autoencoder.py`.

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `WHO_AM_I` value isn't `0x33` | Check the wiring (SDA/SCL swapped?) or the I2C address — if the SDO/SA0 pin is tied to GND, the address may be `0x18`. Try changing `LIS3DH_ADDR` to `0x18` and retry |
| `i2c_master_transmit`-related error (`ESP_ERR_TIMEOUT`, etc.) | Check the I2C pull-up resistors (if not built into the module, you'll need ~4.7kΩ external pull-ups on SDA/SCL), and check the wiring contacts |
| `AllocateTensors() failed` | `kTensorArenaSize` is smaller than what the model needs — retry with a larger value |
| Constant ANOMALY readings | **The most common cause is a training-data collection issue**: if you only collected "normal" data with the board sitting perfectly still, the standard deviation of the reconstruction error ends up close to zero, and the threshold Step 2 computes (mean + 3×std) comes out too small — so even the ordinary variation during real use gets flagged as ANOMALY. The fix is to recollect normal data that includes the kind of movement/vibration you'd actually expect in the deployment environment (see the note box in Step 1 above). If the data itself is fine and this still happens, also check whether the normalization (÷32768.0) matches what was used during training, or whether the quantization scale/zero_point is applied incorrectly — make sure Step 2's normalization and Step 4 match exactly |
| Training went fine but the on-device result looks off | The precision gap between Python (float32) and the ESP32 (INT8 quantized) — re-tune the threshold based on the normal error actually measured on the embedded device |
| `Failed to resolve component 'tflite-lib'` (when building `03_inference`) | Check the location/syntax of `src/idf_component.yml` — see the [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_EN.md) troubleshooting section |
| Building `03_inference` gives `error: either all initializer clauses should be designated or none of them should be` / `expected primary-expression before '.' token` (`main.cc`) | A C++ syntax issue — writing `.flags.enable_internal_pullup = true` to designate a nested member with two dots chained together is **allowed in C as a GCC extension, but not allowed in standard C++**. `01_data_collect` is `main.c` (C) so it compiles fine, but `03_inference` is `main.cc` (C++), which is where this notation fails. Wrapping it in a nested brace-init — `.flags = { .enable_internal_pullup = true },` — compiles correctly in both C and C++ (already applied in the code) |
| Building `03_inference` gives `error: designator order for field '...' does not match declaration order in '...'` (`main.cc`) | Another C++ (C++20)-specific rule — **in C, designated initializers (`.field = value`) can be written in any order, but C++20 requires them to appear in the struct's actual declaration order.** `i2c_master_bus_config_t`'s real declaration order is `i2c_port -> sda_io_num -> scl_io_num -> clk_source -> glitch_ignore_cnt -> ... -> flags`, but the original code listed `clk_source` before `i2c_port` and so on, out of order. `main.cc`'s initializer order was fixed to match the real declaration order (already applied in the code). **As a general rule**: when initializing an ESP-IDF struct field-by-field in a C++ (.cc/.cpp) file, even if you skip some optional fields, **the relative order of the fields you do write must match the header's declaration order** — C files (`main.c`, `oled_status.c`) have no such restriction and can be left as-is |
| OLED screen stays off (log shows `No SSD1306 found`) | Check that SDA/SCL are actually connected to the I2C1 pins (GPIO4/GPIO5) — easy to mix up with the LIS3DH's I2C0 pins (GPIO8/GPIO9). If the wiring is correct, check whether the module's VCC is 3.3V (a 5V-only module won't work) and whether it needs pull-up resistors |
| OLED turns on but the text is garbled or misplaced | The module is 128x32 instead of 128x64 — change `OLED_HEIGHT` to 32 in `oled_status.c`, and change the mux ratio value after `0xA8` to `0x1F` |
| Training (fit) and the threshold print out fine, but `converter.convert()` raises `TypeError: 'NoneType' object is not callable` (inside `keras_deps.get_call_context_function()().enter(...)`) | Recent TensorFlow (2.16+) defaults `tf.keras` to **Keras 3**, which changed the internal call-context API that `tf.lite.TFLiteConverter` expects in its Keras 2-style form — this is a well-known TensorFlow compatibility issue. Training itself works fine (it's pure Keras behavior), and this only shows up at the TFLite conversion step. The fix already applied: `tf_keras` (the legacy Keras 2 compatibility package) was added to `requirements.txt`, and `os.environ["TF_USE_LEGACY_KERAS"] = "1"` is set at the very top of `train_autoencoder.py`, before `import tensorflow`, so TFLite conversion always goes through the Keras 2-compatible path (run `pip install -r requirements.txt` again to pick up `tf_keras`, then rerun). If this environment isn't a venv dedicated to this lab but a venv shared with another project (e.g. an existing `.venv` from another project you just activated), the tensorflow/keras version combination in it may be tangled — in that case also consider creating a fresh venv dedicated to this lab, as described in Step 2 above |
| After `pip install -r requirements.txt`, you see `ERROR: pip's dependency resolver ... requires protobuf<6.0dev... but you have protobuf 7.x` | **The install itself actually succeeded** — this line isn't an install failure, it's pip's post-install compatibility warning that some other package already on your PC (e.g. `grpcio-tools`, installed for a different project) wants a different protobuf version than the one tensorflow just installed. `requirements.txt` (numpy, tensorflow) doesn't use grpcio-tools at all, so it's irrelevant to this lab. If `python -c "import tensorflow as tf; print(tf.__version__)"` prints a version with no error, just go ahead with `python train_autoencoder.py`. If you'd like to get rid of the warning itself, install into a venv dedicated to this lab as described in Step 2 above — isolated from other projects' packages, the conflict never arises in the first place |

## What's next

Labs 01/02 in this track are independent of each other once lab 00 (environment setup) is done — whether or not you've already done `02_WAKE_WORD_LAB` (wake word detection, requires an I2S microphone) makes no difference to finishing this lab. If you haven't tried it yet, feel free to continue on to it.
