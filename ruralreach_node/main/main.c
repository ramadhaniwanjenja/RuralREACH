/*
 * RuralReach Node — OLED + Audio Loopback Test
 *
 * Hold PRG button (GPIO 0): mic → speaker live loopback.
 * The OLED shows status: idle / talking, and a sample counter.
 *
 * Pins:
 *   OLED (built-in):  SDA=17  SCL=18  RST=21  Vext=36
 *   INMP441 mic:      SCK=1   WS=2    SD=3
 *   MAX98357A amp:    BCLK=4  LRC=5   DIN=6
 *   PRG button:       GPIO 0 (active low)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define VEXT_CTRL  GPIO_NUM_36
#define OLED_RST   GPIO_NUM_21
#define OLED_SDA   GPIO_NUM_17
#define OLED_SCL   GPIO_NUM_18
#define OLED_ADDR  0x3C
#define OLED_W     128
#define OLED_H     64

#define MIC_SCK    GPIO_NUM_1
#define MIC_WS     GPIO_NUM_2
#define MIC_SD     GPIO_NUM_3
#define SPK_BCLK   GPIO_NUM_4
#define SPK_LRC    GPIO_NUM_5
#define SPK_DIN    GPIO_NUM_6
#define PTT_BTN    GPIO_NUM_0

#define SAMPLE_RATE 16000
#define BUF_SAMPLES 256

static const char *TAG = "node";
static i2c_master_dev_handle_t oled;
static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;
static uint8_t fb[OLED_W * OLED_H / 8];

// ── 5x7 font (subset) ───────────────────────────────────────────────
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},
};

// ── OLED helpers ─────────────────────────────────────────────────────
static void oled_cmd(uint8_t c) {
    uint8_t buf[2] = {0x00, c};
    i2c_master_transmit(oled, buf, 2, 100);
}
static void oled_data(const uint8_t *d, size_t n) {
    uint8_t buf[129];
    buf[0] = 0x40;
    memcpy(buf + 1, d, n);
    i2c_master_transmit(oled, buf, n + 1, 100);
}
static void oled_clear(void) { memset(fb, 0, sizeof(fb)); }
static void oled_draw_char(int x, int page, char c) {
    if (c < 32 || c > 'z') c = '?';
    if (x < 0 || x > OLED_W - 6 || page < 0 || page > 7) return;
    const uint8_t *g = font5x7[c - 32];
    for (int i = 0; i < 5; i++) fb[page * OLED_W + x + i] = g[i];
}
static void oled_draw_string(int x, int page, const char *s) {
    while (*s) { oled_draw_char(x, page, *s); x += 6; s++; }
}
static void oled_flush(void) {
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);
    for (int p = 0; p < 8; p++) oled_data(&fb[p * OLED_W], OLED_W);
}

static void oled_init(void) {
    gpio_config_t v = { .pin_bit_mask = 1ULL << VEXT_CTRL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&v); gpio_set_level(VEXT_CTRL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_config_t r = { .pin_bit_mask = 1ULL << OLED_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&r);
    gpio_set_level(OLED_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_RST, 1); vTaskDelay(pdMS_TO_TICKS(20));

    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0, .sda_io_num = OLED_SDA, .scl_io_num = OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    i2c_new_master_bus(&bc, &bus);
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDR, .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(bus, &dc, &oled);

    uint8_t seq[] = {
        0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,0x20,0x00,
        0xA1,0xC8,0xDA,0x12,0x81,0xCF,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF
    };
    for (size_t i = 0; i < sizeof(seq); i++) oled_cmd(seq[i]);
    ESP_LOGI(TAG, "OLED initialised");
}

// ── Mic + speaker init ───────────────────────────────────────────────
static void mic_init(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&cc, NULL, &rx_handle);
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK, .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED, .din = MIC_SD,
        },
    };
    i2s_channel_init_std_mode(rx_handle, &cfg);
    i2s_channel_enable(rx_handle);
    ESP_LOGI(TAG, "Mic (INMP441) ready");
}

static void spk_init(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&cc, &tx_handle, NULL);
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK, .ws = SPK_LRC,
            .dout = SPK_DIN, .din = I2S_GPIO_UNUSED,
        },
    };
    i2s_channel_init_std_mode(tx_handle, &cfg);
    i2s_channel_enable(tx_handle);
    ESP_LOGI(TAG, "Speaker (MAX98357A) ready");
}

static void button_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PTT_BTN,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "PTT button ready");
}

// ── UI: redraw whole status screen ───────────────────────────────────
static void ui_draw(bool talking, int counter) {
    oled_clear();
    oled_draw_string(0, 0, "RuralReach Node");
    oled_draw_string(0, 1, "433 MHz V3");
    oled_draw_string(0, 3, talking ? "  >> TALKING <<" : "  Hold PRG btn");
    char line[24];
    snprintf(line, sizeof(line), "Audio samples:%d", counter);
    oled_draw_string(0, 5, line);
    oled_draw_string(0, 7, talking ? "Mic --> Speaker" : "Idle           ");
    oled_flush();
}

// ── Main ─────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "RuralReach booting...");
    oled_init();
    button_init();
    mic_init();
    spk_init();

    ui_draw(false, 0);

    int32_t mic_buf[BUF_SAMPLES];
    int16_t spk_buf[BUF_SAMPLES];
    size_t bytes_in = 0, bytes_out = 0;
    int total_samples = 0;
    bool was_pressed = false;
    int ui_counter = 0;

    while (1) {
        bool pressed = (gpio_get_level(PTT_BTN) == 0);

        if (pressed != was_pressed) {
            ESP_LOGI(TAG, "%s", pressed ? ">> PTT pressed" : "<< PTT released");
            ui_draw(pressed, total_samples);
        }
        was_pressed = pressed;

        if (pressed) {
            i2s_channel_read(rx_handle, mic_buf, sizeof(mic_buf),
                             &bytes_in, pdMS_TO_TICKS(100));
            int n = bytes_in / sizeof(int32_t);
            for (int i = 0; i < n; i++) {
                int32_t s = mic_buf[i] >> 14;
                if (s >  32767) s =  32767;
                if (s < -32768) s = -32768;
                spk_buf[i] = (int16_t)s;
            }
            i2s_channel_write(tx_handle, spk_buf, n * sizeof(int16_t),
                              &bytes_out, pdMS_TO_TICKS(100));
            total_samples += n;

            // Refresh UI every ~16 audio frames so the screen tracks activity
            if (++ui_counter > 16) {
                ui_counter = 0;
                ui_draw(true, total_samples);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
