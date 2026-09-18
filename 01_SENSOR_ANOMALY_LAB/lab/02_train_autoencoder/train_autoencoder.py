"""
01_SENSOR_ANOMALY_LAB - Step 2: Train an Autoencoder on normal-state data (PC-side).

Input:  normal_data.csv (produced by Step 1, one line = SAMPLE_WINDOW *
        AXES comma-separated raw LIS3DH readings (x0,y0,z0,x1,y1,z1,...),
        copied here from the board's serial monitor)
Output: anomaly_model.tflite (INT8-quantized model, ready for Step 3's
        conversion to a C array for on-device inference)

Run:
    pip install -r requirements.txt
    python train_autoencoder.py
"""

# Must be set before "import tensorflow" - recent TensorFlow releases (2.16+)
# default tf.keras to Keras 3, whose internal call-context API changed in a
# way that breaks tf.lite.TFLiteConverter.from_keras_model() with
# "TypeError: 'NoneType' object is not callable" at convert() time. Forcing
# the legacy Keras 2 implementation (via the tf_keras package, see
# requirements.txt) keeps tf.keras fully compatible with the TFLite
# converter - this is TensorFlow's own documented workaround, not a hack.
import os
os.environ.setdefault("TF_USE_LEGACY_KERAS", "1")

import numpy as np
import tensorflow as tf
from tensorflow import keras

SAMPLE_WINDOW = 20          # readings per pattern - must match src/main.c
AXES = 3                    # X, Y, Z from the LIS3DH
FEATURE_LEN = SAMPLE_WINDOW * AXES   # 60
RAW_FULL_SCALE = 32768.0    # normalizes raw signed 16-bit accel counts to roughly [-1, 1]

# --- Load and normalize the training data -----------------------------------
# normal_data.csv comes straight from redirecting the serial monitor
# ("pio device monitor > normal_data.csv"), so its first and/or last line is
# often a partial window: the monitor can attach mid-print, and Ctrl+C can
# land mid-print too. Rather than fail the whole load on one bad line
# (np.loadtxt requires every row to have the same column count), read the
# file line by line and skip any row that doesn't have exactly FEATURE_LEN
# comma-separated numbers, reporting how many were skipped.
def load_normal_data(path, feature_len):
    rows = []
    skipped = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) != feature_len:
                skipped += 1
                continue
            try:
                rows.append([float(p) for p in parts])
            except ValueError:
                skipped += 1
                continue
    if skipped:
        print(f"Skipped {skipped} malformed line(s) in {path} "
              f"(expected {feature_len} values per line - usually just the "
              "partial first/last line from starting/stopping the serial monitor "
              "mid-window, safe to ignore)")
    if not rows:
        raise ValueError(
            f"No valid {feature_len}-column rows found in {path} - "
            "check SAMPLE_WINDOW/AXES match src/main.c, and that the file "
            "actually has CSV data in it"
        )
    return np.array(rows, dtype=np.float32)

data = load_normal_data("normal_data.csv", FEATURE_LEN)
data = data / RAW_FULL_SCALE  # normalize to roughly -1..1

print(f"Loaded {data.shape[0]} windows of {data.shape[1]} values each")

# --- Build a small Autoencoder: FEATURE_LEN -> 16 -> FEATURE_LEN ------------
# Decoder uses tanh (not sigmoid) because normalized accelerometer values can
# be negative - sigmoid's 0..1 output range would clip half the signal.
inputs = keras.Input(shape=(FEATURE_LEN,))
encoded = keras.layers.Dense(16, activation="relu")(inputs)
decoded = keras.layers.Dense(FEATURE_LEN, activation="tanh")(encoded)
autoencoder = keras.Model(inputs, decoded)
autoencoder.compile(optimizer="adam", loss="mse")

autoencoder.fit(data, data, epochs=50, batch_size=16, validation_split=0.1)

# --- Determine a recommended anomaly threshold from normal-data errors ------
reconstructed = autoencoder.predict(data)
errors = np.mean((data - reconstructed) ** 2, axis=1)
print("Normal-data reconstruction error - mean:", errors.mean(), "max:", errors.max())
threshold = errors.mean() + 3 * errors.std()
print("Recommended ANOMALY_THRESHOLD:", threshold)
print("Copy this value into lab/03_inference/src/main.cc's ANOMALY_THRESHOLD macro.")

# --- Quantize to INT8 and export as .tflite ----------------------------------
def representative_dataset():
    for sample in data[: min(100, len(data))]:
        yield [sample.reshape(1, FEATURE_LEN).astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(autoencoder)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8
tflite_model = converter.convert()

with open("anomaly_model.tflite", "wb") as f:
    f.write(tflite_model)

print("Wrote anomaly_model.tflite - proceed to Step 3 (convert to C array).")
