// 02_custom_wakeword - continuous wake-word inference with an Edge Impulse
// model, using the same I2S digital microphone wiring as 01_yesno_demo.
//
// The Edge Impulse SDK you copy into this project (see src/CMakeLists.txt)
// ships a static single-shot example (a hardcoded feature array fed once to
// run_classifier()). Real-time wake-word detection instead needs CONTINUOUS
// inference: audio keeps streaming in while the classifier looks at a
// sliding window of it. This file implements that streaming pipeline:
//
//   1. A FreeRTOS task blocks on I2S reads and fills a double buffer,
//      one "slice" of samples at a time (mirrors the I2S setup already
//      verified in 01_yesno_demo/src/i2s_setup.cc).
//   2. The main loop waits for each finished slice and hands it to Edge
//      Impulse's run_classifier_continuous(), which internally maintains
//      the sliding window across calls.
//   3. Once the window has filled with real audio (after
//      EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW slices), results are printed
//      and checked against a detection threshold.
//
// This double-buffer / slice pattern mirrors Edge Impulse's own continuous
// audio example (originally written for Arduino + a PDM microphone,
// "nano_ble33_sense_microphone_continuous.ino"); the code below is a
// from-scratch ESP-IDF/I2S port of that same pattern, not a copy of it.

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"   // for numpy::int16_to_float()

static const char *TAG = "EI_WAKEWORD";

// ---------------------------------------------------------------------
// Wiring - identical to 01_yesno_demo (see the KR doc's wiring table).
// ---------------------------------------------------------------------
#define MIC_BCLK_GPIO   GPIO_NUM_16   // SCK  (bit clock)
#define MIC_WS_GPIO     GPIO_NUM_15   // WS   (word select / LRCLK)
#define MIC_DIN_GPIO    GPIO_NUM_17   // SD   (serial data, mic -> board)

// NOTE: Edge Impulse Studio lets you pick the audio sample rate when you
// create the Audio (MFE/MFCC) processing block - 16000 Hz is the default
// and what this wiring/driver is configured for below. If your Studio
// project used a different rate, change SAMPLE_RATE_HZ to match EXACTLY -
// a mismatch does not error out, it just silently degrades accuracy.
static constexpr uint32_t SAMPLE_RATE_HZ = 16000;

// A detection is reported when the highest-scoring label is at or above
// this confidence AND is not one of the background labels below. Loosen
// this (e.g. 0.7) if real detections are being missed, or raise it
// (e.g. 0.9) if background noise triggers too many false positives -
// same trade-off as tuning any classifier threshold.
static constexpr float DETECTION_THRESHOLD = 0.8f;

// Adjust these to match whatever you named your negative/background
// classes in the Edge Impulse Studio Data Acquisition step (commonly
// "noise" and "unknown", sometimes prefixed with an underscore).
static bool is_background_label(const char *label)
{
    return (strcmp(label, "noise") == 0) ||
           (strcmp(label, "unknown") == 0) ||
           (strcmp(label, "_unknown") == 0) ||
           (strcmp(label, "_noise") == 0);
}

// ---------------------------------------------------------------------
// I2S microphone setup - I2S STD mode, mono, 32-bit slot width. This is
// the same driver/configuration already verified working in
// 01_yesno_demo/src/i2s_setup.cc for this exact mic (INMP441) and wiring.
// ---------------------------------------------------------------------
static i2s_chan_handle_t rx_handle = nullptr;

static void i2s_mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        // INMP441 outputs 24-bit samples left-justified in a 32-bit I2S
        // word, so we read the slot as 32-bit even though the true audio
        // resolution is lower.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_GPIO,
            .ws = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // L/R tied to GND (see wiring table) -> mic answers on the left slot.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

// The INMP441's 24-bit sample sits in the top bits of the 32-bit I2S word.
// Shifting right by 14 (rather than the naive 16) matches the gain used by
// esp-tflite-micro's own i2s_setup.cc for this same mic/board combination
// (see 01_yesno_demo) - keeping it consistent means a model trained on
// audio captured with one lab's gain behaves the same in the other.
static inline int16_t raw32_to_int16(int32_t raw)
{
    return (int16_t)(raw >> 14);
}

// ---------------------------------------------------------------------
// Double buffer shared between the I2S capture task and the main loop.
// While the classifier consumes one buffer, the capture task fills the
// other; buf_select tracks which one is CURRENTLY being written.
// ---------------------------------------------------------------------
struct inference_buffers_t {
    int16_t *buffers[2];
    uint32_t buf_count;   // samples written into the active buffer so far
    uint32_t n_samples;   // samples per slice (= EI_CLASSIFIER_SLICE_SIZE)
    uint8_t buf_select;   // which buffer index the capture task is filling
};

static inference_buffers_t inference;
static SemaphoreHandle_t slice_ready_sem;

static void capture_task(void *arg)
{
    // One I2S read grabs a small chunk; a chunk is much smaller than a full
    // slice so the semaphore is given promptly and inference latency stays
    // low. 256 samples @16kHz/32-bit is a good balance of DMA overhead vs.
    // responsiveness.
    static constexpr size_t kChunkSamples = 256;
    int32_t raw_buf[kChunkSamples];

    while (true) {
        size_t bytes_read = 0;
        i2s_channel_read(rx_handle, raw_buf, sizeof(raw_buf), &bytes_read, portMAX_DELAY);
        size_t samples_read = bytes_read / sizeof(int32_t);

        for (size_t i = 0; i < samples_read; i++) {
            inference.buffers[inference.buf_select][inference.buf_count++] =
                raw32_to_int16(raw_buf[i]);

            if (inference.buf_count >= inference.n_samples) {
                // Slice is full - hand it off and start filling the OTHER
                // buffer so the main loop can safely read this one.
                inference.buf_select ^= 1;
                inference.buf_count = 0;
                xSemaphoreGive(slice_ready_sem);
            }
        }
    }
}

static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffers[0] = (int16_t *)malloc(n_samples * sizeof(int16_t));
    inference.buffers[1] = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (inference.buffers[0] == nullptr || inference.buffers[1] == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate audio slice buffers (%lu samples each)",
                 (unsigned long)n_samples);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;

    slice_ready_sem = xSemaphoreCreateBinary();

    i2s_mic_init();
    xTaskCreate(capture_task, "capture_task", 4096, NULL, 10, NULL);
    return true;
}

// Blocks until the capture task has finished filling one full slice.
static void microphone_inference_wait_slice(void)
{
    xSemaphoreTake(slice_ready_sem, portMAX_DELAY);
}

// Edge Impulse's signal_t calls this to pull `length` float samples,
// starting at `offset`, out of whichever buffer the capture task just
// FINISHED writing (i.e. NOT the one it is currently filling - that's
// buf_select ^ 1 at this point, since buf_select was already flipped).
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    int16_t *filled_buffer = inference.buffers[inference.buf_select ^ 1];
    numpy::int16_to_float(&filled_buffer[offset], out_ptr, length);
    return 0;
}

extern "C" int app_main(void)
{
    ei_printf("Edge Impulse custom wake word - continuous inference (ESP32-S3 + I2S mic)\n");
    ei_printf("Labels in this model:\n");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("  - %s\n", ei_classifier_inferencing_categories[ix]);
    }

    if (EI_CLASSIFIER_FREQUENCY != (int)SAMPLE_RATE_HZ) {
        ei_printf("WARN: model sample rate (%d Hz) != mic sample rate (%lu Hz).\n",
                  EI_CLASSIFIER_FREQUENCY, (unsigned long)SAMPLE_RATE_HZ);
        ei_printf("      Update SAMPLE_RATE_HZ in main.cc to match your Studio project.\n");
    }

    // Resets the classifier's internal sliding-window state. Required once
    // before the first run_classifier_continuous() call, and again if you
    // ever want to "start fresh" (e.g. after a long silence).
    run_classifier_init();

    if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
        ei_printf("ERR: failed to start microphone, halting\n");
        return 1;
    }

    // The first few slices only partially populate the model's window with
    // real audio (the rest is zero-initialized) - their classification
    // output is not meaningful. Skip printing until the window has been
    // filled once end-to-end.
    uint32_t slices_seen = 0;

    while (true) {
        microphone_inference_wait_slice();

        signal_t signal;
        signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
        signal.get_data = &microphone_audio_signal_get_data;

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier_continuous(&signal, &result, false /* debug */);
        if (err != EI_IMPULSE_OK) {
            ei_printf("ERR: run_classifier_continuous failed (%d)\n", err);
            continue;
        }

        slices_seen++;
        if (slices_seen < EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW) {
            continue;
        }

        size_t best_ix = 0;
        for (size_t ix = 1; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > result.classification[best_ix].value) {
                best_ix = ix;
            }
        }

        ei_printf("Predictions (DSP %d ms, classification %d ms):\n",
                  result.timing.dsp, result.timing.classification);
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("  %s: %.5f\n", result.classification[ix].label,
                      result.classification[ix].value);
        }

        if (result.classification[best_ix].value >= DETECTION_THRESHOLD &&
            !is_background_label(result.classification[best_ix].label)) {
            // This is the point to swap in your own reaction - light an
            // LED, trigger a GPIO, publish MQTT, etc. - the same pattern
            // 01_yesno_demo's command_responder.cc uses for "yes"/"no".
            ei_printf(">>> WAKE WORD DETECTED: %s (%.2f)\n",
                      result.classification[best_ix].label,
                      result.classification[best_ix].value);
        }
    }
}
