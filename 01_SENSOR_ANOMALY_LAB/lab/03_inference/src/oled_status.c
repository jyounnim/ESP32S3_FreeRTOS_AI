/*
 * Minimal SSD1306 status display driver (I2C1, horizontal addressing mode).
 * Only implements what this lab needs: init + clear + draw up to 3 lines of
 * 8x8 text. See oled_status.h for the API and font8x8_basic.h for credits.
 */

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "oled_status.h"
#include "font8x8_basic.h"

#define OLED_SDA_GPIO   GPIO_NUM_4
#define OLED_SCL_GPIO   GPIO_NUM_5
#define OLED_I2C_PORT   I2C_NUM_1
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)
#define OLED_MAX_CHARS_PER_LINE (OLED_WIDTH / 8)  // 16

#define OLED_CTRL_CMD_STREAM   0x00
#define OLED_CTRL_DATA_STREAM  0x40

static const char *TAG = "OLED_STATUS";
static i2c_master_dev_handle_t s_oled;
static bool s_ready = false;

static void oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { OLED_CTRL_CMD_STREAM, cmd };
    i2c_master_transmit(s_oled, buf, sizeof(buf), -1);
}

static void oled_cmd2(uint8_t cmd, uint8_t arg)
{
    uint8_t buf[3] = { OLED_CTRL_CMD_STREAM, cmd, arg };
    i2c_master_transmit(s_oled, buf, sizeof(buf), -1);
}

static void oled_set_window(uint8_t col_start, uint8_t col_end, uint8_t page_start, uint8_t page_end)
{
    uint8_t buf[7] = {
        OLED_CTRL_CMD_STREAM,
        0x21, col_start, col_end,    // set column address range
        0x22, page_start, page_end,  // set page address range
    };
    i2c_master_transmit(s_oled, buf, sizeof(buf), -1);
}

bool oled_status_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = OLED_I2C_PORT,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_io_num = OLED_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create I2C1 bus - status display disabled");
        return false;
    }

    // Auto-probe both common SSD1306 addresses (module-dependent, same as
    // this series' other SSD1306 labs).
    uint8_t addr;
    if (i2c_master_probe(bus, 0x3C, 50) == ESP_OK) {
        addr = 0x3C;
    } else if (i2c_master_probe(bus, 0x3D, 50) == ESP_OK) {
        addr = 0x3D;
    } else {
        ESP_LOGW(TAG, "No SSD1306 found at 0x3C or 0x3D - status display disabled");
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_oled) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to attach SSD1306 device - status display disabled");
        return false;
    }

    // Standard SSD1306 128x64 init sequence
    oled_cmd(0xAE);          // display off
    oled_cmd2(0xD5, 0x80);   // clock divide ratio / oscillator freq
    oled_cmd2(0xA8, 0x3F);   // mux ratio = 64 (0x3F = 63 -> 64 rows)
    oled_cmd2(0xD3, 0x00);   // display offset = 0
    oled_cmd(0x40);          // display start line = 0
    oled_cmd2(0x8D, 0x14);   // charge pump enable
    oled_cmd2(0x20, 0x00);   // memory addressing mode = horizontal
    oled_cmd(0xA1);          // segment remap (column 127 -> SEG0)
    oled_cmd(0xC8);          // COM output scan direction remapped
    oled_cmd2(0xDA, 0x12);   // COM pins hardware config (128x64)
    oled_cmd2(0x81, 0xCF);   // contrast
    oled_cmd2(0xD9, 0xF1);   // pre-charge period
    oled_cmd2(0xDB, 0x40);   // VCOMH deselect level
    oled_cmd(0xA4);          // resume RAM content display (not all-on)
    oled_cmd(0xA6);          // normal display (not inverted)
    oled_cmd(0xAF);          // display on

    s_ready = true;
    ESP_LOGI(TAG, "SSD1306 status display ready at 0x%02X", addr);
    return true;
}

static void oled_draw_line(uint8_t page, const char *text)
{
    size_t len = strlen(text);
    if (len > OLED_MAX_CHARS_PER_LINE) {
        len = OLED_MAX_CHARS_PER_LINE;
    }
    if (len == 0) {
        return;
    }

    uint8_t col_end = (uint8_t)(len * 8 - 1);
    oled_set_window(0, col_end, page, page);

    uint8_t buf[1 + OLED_MAX_CHARS_PER_LINE * 8];
    buf[0] = OLED_CTRL_DATA_STREAM;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c > 0x7F) {
            c = '?';
        }
        memcpy(&buf[1 + i * 8], font8x8_basic_tr[c], 8);
    }
    i2c_master_transmit(s_oled, buf, 1 + len * 8, -1);
}

static void oled_clear(void)
{
    oled_set_window(0, OLED_WIDTH - 1, 0, OLED_PAGES - 1);
    uint8_t buf[1 + OLED_WIDTH];
    buf[0] = OLED_CTRL_DATA_STREAM;
    memset(&buf[1], 0x00, OLED_WIDTH);
    for (int page = 0; page < OLED_PAGES; page++) {
        i2c_master_transmit(s_oled, buf, sizeof(buf), -1);
    }
}

void oled_status_show(const char *title, const char *line1, const char *line2)
{
    if (!s_ready) {
        return;
    }
    oled_clear();
    oled_draw_line(0, title);
    oled_draw_line(2, line1);
    oled_draw_line(4, line2);
}
