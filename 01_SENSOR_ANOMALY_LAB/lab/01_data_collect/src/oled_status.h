/*
 * Minimal SSD1306 status display (I2C1, SDA=GPIO4, SCL=GPIO5).
 * Shows which step/program is running and its live status - purely a
 * "what's happening right now" readout, not required for the ML pipeline
 * itself. If no display responds, the program keeps running without one.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Probes 0x3C then 0x3D and initializes the display if found.
// Returns false (non-fatal) if no SSD1306 responds on I2C1.
bool oled_status_init(void);

// Clears the screen and draws up to 3 short lines (<=16 chars each,
// anything longer is truncated - keep lines short).
void oled_status_show(const char *title, const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
