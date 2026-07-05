/*
 * RuralReach Gateway — ESP32-S3 + SX1262 (Heltec V3) + SIM800L
 * ------------------------------------------------------------
 * Listens for LoRa packets from the node (433 MHz / SF9 / BW125, sync 0x1424),
 * expects the plaintext frame "<phone>|<body>", and sends it as an SMS through
 * a SIM800L over UART. Same hardware/format as ruralreach_node.
 *
 * Pins:
 *   OLED (built-in):  SDA=17  SCL=18  RST=21  Vext=36
 *   SX1262 LoRa:      NSS=8 SCK=9 MOSI=10 MISO=11 RST=12 BUSY=13 DIO1=14
 *   SIM800L (UART1):  ESP TX=46 -> SIM RXD,  ESP RX=48 <- SIM TXD,  9600 8N1
 *                     (GPIO45 avoided: it's the VDD_SPI strapping pin.)
 *   SIM800L power:    3.7-4.2 V @ >=2 A on its own supply, common GND.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "lora_driver.h"
#include "codec2.h"

#define VEXT_CTRL  GPIO_NUM_36
#define OLED_RST   GPIO_NUM_21
#define OLED_SDA   GPIO_NUM_17
#define OLED_SCL   GPIO_NUM_18
#define OLED_ADDR  0x3C
#define OLED_W     128
#define OLED_H     64

#define SIM_UART   UART_NUM_1
#define SIM_TX_PIN GPIO_NUM_46   /* -> SIM800L RXD */
#define SIM_RX_PIN GPIO_NUM_48   /* <- SIM800L TXD */
#define SIM_BAUD   9600

/* Forward audio INTO the SIM800L call: the ESP32 has no DAC, so it outputs the
 * received voice as a PDM pulse stream on GPIO7; an external R-C filter turns
 * it into analog audio for the SIM800L MIC+. GPIO42 is the PDM clock — leave it
 * UNCONNECTED. */
#define PDM_OUT_PIN GPIO_NUM_7    /* -> R-C filter -> SIM800L MIC+ */
#define PDM_CLK_PIN GPIO_NUM_42   /* PDM clock; leave unconnected */

/* DIAGNOSTIC: set to 1 to send a steady 1 kHz tone into the SIM800L MIC
 * (bypasses LoRa/Codec2). If the receiver hears a beep during a call, the
 * analog wiring + SIM800L mic are good. Set back to 0 for normal voice. */
#define AUDIO_TEST_TONE  0
/* Reverse audio OUT of the SIM800L call: SPK+ -> coupling cap + divider ->
 * ESP32 ADC (GPIO4 = ADC1 channel 3) -> Codec2 -> LoRa -> node speaker. */
#define ADC_SPK_CHANNEL  ADC_CHANNEL_3   /* GPIO4 */
#define REV_GAIN_SHIFT   5               /* reverse mic gain (bigger = louder) */

#define SAMPLE_RATE   8000       /* Codec2 = 8 kHz */
#define VOICE_MODE            CODEC2_MODE_1300  /* must match the node */
#define VOICE_FRAME_BYTES     7
#define VOICE_FRAMES_PER_PKT  12
#define VOICE_MARKER  0x01       /* first byte of a voice packet (vs ASCII SMS) */
#define MIC_GAIN_SHIFT 4
#define MIC_FORCE_CH   (-1)      /* -1 = auto-pick the live INMP441 channel */

static const char *TAG = "gw";
static i2c_master_dev_handle_t oled;
static uint8_t fb[OLED_W * OLED_H / 8];
static i2s_chan_handle_t spk_handle;
static adc_continuous_handle_t adc_handle;
static QueueHandle_t play_q;     /* Codec2 frames: RX loop -> decode -> PDM/MIC */
static QueueHandle_t voice_q;    /* Codec2 frames: ADC(SPK) capture -> reverse LoRa TX */
static volatile bool g_in_call = false;

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
static void oled_draw_wrapped(int page, int max_pages, const char *s) {
    char line[22];
    int used = 0;
    while (*s && used < max_pages) {
        int n = 0;
        while (s[n] && n < 21) n++;
        memcpy(line, s, n);
        line[n] = '\0';
        oled_draw_string(0, page + used, line);
        s += n;
        used++;
    }
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

static void ui_show(const char *l0, const char *l3, const char *l5wrap) {
    oled_clear();
    oled_draw_string(0, 0, "RuralReach GW");
    if (l0)     oled_draw_string(0, 1, l0);
    if (l3)     oled_draw_string(0, 3, l3);
    if (l5wrap) oled_draw_wrapped(5, 3, l5wrap);
    oled_flush();
}

// ── SIM800L over UART ────────────────────────────────────────────────
static void sim_init(void) {
    uart_config_t cfg = {
        .baud_rate = SIM_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(SIM_UART, 2048, 0, 0, NULL, 0);
    uart_param_config(SIM_UART, &cfg);
    uart_set_pin(SIM_UART, SIM_TX_PIN, SIM_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "SIM800L UART ready (TX=%d RX=%d @ %d)",
             SIM_TX_PIN, SIM_RX_PIN, SIM_BAUD);
}

/* Read modem output until `marker` appears or timeout; keep last bytes in out. */
static bool sim_wait(const char *marker, int timeout_ms, char *out, size_t cap) {
    int64_t t0 = esp_timer_get_time();
    size_t n = 0;
    out[0] = '\0';
    while ((esp_timer_get_time() - t0) / 1000 < timeout_ms) {
        uint8_t c;
        int r = uart_read_bytes(SIM_UART, &c, 1, pdMS_TO_TICKS(50));
        if (r > 0 && n < cap - 1) {
            out[n++] = (char)c;
            out[n] = '\0';
            if (strstr(out, marker)) return true;
        }
    }
    return false;
}

static bool sim_at(const char *cmd, const char *marker, int timeout_ms) {
    char resp[256];
    uart_flush_input(SIM_UART);
    uart_write_bytes(SIM_UART, cmd, strlen(cmd));
    uart_write_bytes(SIM_UART, "\r", 1);
    bool ok = sim_wait(marker, timeout_ms, resp, sizeof(resp));
    ESP_LOGI(TAG, "AT %-14s => %s", cmd, resp);
    return ok;
}

static bool sim_send_sms(const char *phone, const char *body) {
    char resp[256];
    sim_at("AT+CMGF=1", "OK", 1500);
    uart_flush_input(SIM_UART);

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "AT+CMGS=\"%s\"\r", phone);
    uart_write_bytes(SIM_UART, hdr, strlen(hdr));
    sim_wait(">", 3000, resp, sizeof(resp));         /* prompt */

    uart_write_bytes(SIM_UART, body, strlen(body));
    char ctrlz = 26;
    uart_write_bytes(SIM_UART, &ctrlz, 1);           /* send */

    bool ok = sim_wait("+CMGS", 20000, resp, sizeof(resp));
    ESP_LOGI(TAG, "SMS result: %s", resp);
    return ok;
}

/* Check for an incoming SMS (a reply). If found, forward it to the node over
 * LoRa as "<sender>|<body>", then delete it. Called periodically. */
static void poll_incoming_sms(void) {
    static char resp[640];
    uart_flush_input(SIM_UART);
    const char *cmd = "AT+CMGL=\"REC UNREAD\"";
    uart_write_bytes(SIM_UART, cmd, strlen(cmd));
    uart_write_bytes(SIM_UART, "\r", 1);
    sim_wait("OK", 3000, resp, sizeof(resp));

    char *h = strstr(resp, "+CMGL:");
    if (!h) return;                        /* nothing new */

    int idx = atoi(h + 6);                 /* message index */

    /* fields: +CMGL: <idx>,"REC UNREAD","<sender>",... -> sender is 3rd quote pair */
    char *q[4]; int qn = 0; char *qp = h;
    while (qn < 4 && (qp = strchr(qp, '"'))) { q[qn++] = qp; qp++; }
    if (qn < 4) return;

    char sender[24];
    int sl = q[3] - q[2] - 1;
    if (sl < 0) sl = 0;
    if (sl > 23) sl = 23;
    memcpy(sender, q[2] + 1, sl);
    sender[sl] = '\0';

    char *nl = strchr(q[3], '\n');         /* body starts on the next line */
    if (!nl) return;
    char body[160]; int bl = 0;
    for (char *b = nl + 1; *b && *b != '\r' && *b != '\n' && bl < 159; b++)
        body[bl++] = *b;
    body[bl] = '\0';

    ESP_LOGI(TAG, "SMS reply from %s: \"%s\" -> LoRa to node", sender, body);
    ui_show("Reply -> node", sender, body);

    char frame[200];
    snprintf(frame, sizeof(frame), "%s|%s", sender, body);
    lora_send((const uint8_t *)frame, strlen(frame));

    char del[24];
    snprintf(del, sizeof(del), "AT+CMGD=%d", idx);
    sim_at(del, "OK", 2000);

    ui_show("Listening 433...", NULL, NULL);
}

// ── Audio out to SIM800L MIC via I2S PDM (GPIO7 -> R-C filter -> MIC+) ─
static void spk_init(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Output silence (not a repeat of the last buffer) between voice packets.
    cc.auto_clear = true;
    i2s_new_channel(&cc, &spk_handle, NULL);
    i2s_pdm_tx_config_t cfg = {
        .clk_cfg  = I2S_PDM_TX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk  = PDM_CLK_PIN,
            .dout = PDM_OUT_PIN,
            .invert_flags = { .clk_inv = false },
        },
    };
    i2s_channel_init_pdm_tx_mode(spk_handle, &cfg);
    i2s_channel_enable(spk_handle);
    ESP_LOGI(TAG, "PDM audio out ready (GPIO%d -> R-C -> SIM800L MIC)", PDM_OUT_PIN);
}

// ── ADC in: read the SIM800L SPK output (reverse audio) ──────────────
static void adc_init(void) {
    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = 4096,
        .conv_frame_size = 256 * SOC_ADC_DIGI_RESULT_BYTES,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&hcfg, &adc_handle));
    adc_digi_pattern_config_t pat = {
        .atten = ADC_ATTEN_DB_12,        // full 0..3.3 V range
        .channel = ADC_SPK_CHANNEL,
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };
    adc_continuous_config_t ccfg = {
        .sample_freq_hz = SAMPLE_RATE,   // 8 kHz
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = 1,
        .adc_pattern = &pat,
    };
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &ccfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    ESP_LOGI(TAG, "ADC ready (GPIO4/ADC1_CH3 <- SIM800L SPK @ %d Hz)", SAMPLE_RATE);
}

// ── Voice playback task: play_q (Codec2 frames) -> decode -> speaker ──
// Big-stack task because Codec2 decode needs ~32 KB.
static void voice_play_task(void *arg) {
    struct CODEC2 *c2 = codec2_create(VOICE_MODE);
    if (!c2) { ESP_LOGE(TAG, "codec2_create failed"); vTaskDelete(NULL); return; }
    int nsam = codec2_samples_per_frame(c2);   // 320
    ESP_LOGI(TAG, "Voice decode ready (Codec2 1300, %d samples/frame)", nsam);
    static int16_t pcm[320];
    uint8_t frame[VOICE_FRAME_BYTES];
    size_t wrote;
    while (1) {
        if (xQueueReceive(play_q, frame, portMAX_DELAY) == pdTRUE) {
            codec2_decode(c2, pcm, frame);
            i2s_channel_write(spk_handle, pcm, nsam * sizeof(int16_t),
                              &wrote, pdMS_TO_TICKS(200));
        }
    }
}

// ── DIAGNOSTIC test tone: 1 kHz sine straight into the PDM/MIC output ─
static void tone_task(void *arg) {
    static int16_t buf[160];        // 20 ms at 8 kHz
    float ph = 0.0f;
    size_t wrote;
    ESP_LOGW(TAG, "TEST TONE mode: sending 1 kHz into the SIM800L MIC");
    while (1) {
        for (int i = 0; i < 160; i++) {
            buf[i] = (int16_t)(9000.0f * sinf(ph));
            ph += 2.0f * 3.14159265f * 1000.0f / 8000.0f;   // 1 kHz
            if (ph > 6.2831853f) ph -= 6.2831853f;
        }
        i2s_channel_write(spk_handle, buf, sizeof(buf), &wrote, portMAX_DELAY);
    }
}

// ── ADC capture task: SIM800L SPK -> ADC -> Codec2 -> voice_q ─────────
// Runs only during a call. Accumulates 320-sample frames and encodes them.
static void adc_capture_task(void *arg) {
    struct CODEC2 *c2 = codec2_create(VOICE_MODE);
    if (!c2) { ESP_LOGE(TAG, "codec2_create (adc) failed"); vTaskDelete(NULL); return; }
    ESP_LOGI(TAG, "ADC capture ready (SIM800L SPK -> LoRa -> node)");
    static uint8_t adcbuf[256 * SOC_ADC_DIGI_RESULT_BYTES];
    static int16_t pcm[320];
    static uint8_t bits[16];
    int filled = 0;
    int32_t dc = 2048;                    // ADC mid-rail bias tracker
    while (1) {
        if (!g_in_call) { vTaskDelay(pdMS_TO_TICKS(20)); filled = 0; continue; }
        uint32_t got = 0;
        if (adc_continuous_read(adc_handle, adcbuf, sizeof(adcbuf), &got,
                                100) != ESP_OK) continue;
        int n = got / SOC_ADC_DIGI_RESULT_BYTES;
        for (int i = 0; i < n; i++) {
            adc_digi_output_data_t *d =
                (adc_digi_output_data_t *)&adcbuf[i * SOC_ADC_DIGI_RESULT_BYTES];
            int raw = d->type2.data;          // 0..4095
            dc += (raw - dc) >> 10;           // track & remove the DC bias
            int32_t s = (raw - dc) << REV_GAIN_SHIFT;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            pcm[filled++] = (int16_t)s;
            if (filled >= 320) {
                codec2_encode(c2, bits, pcm);
                xQueueSend(voice_q, bits, 0);
                filled = 0;
            }
        }
    }
}

// ── Main ─────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "RuralReach gateway booting...");
    oled_init();
    ui_show("starting...", NULL, NULL);

    sim_init();
    vTaskDelay(pdMS_TO_TICKS(1500));
    sim_at("AT", "OK", 1500);
    sim_at("AT+CMGF=1", "OK", 1500);
    sim_at("AT+CSCS=\"GSM\"", "OK", 1500);
    sim_at("AT+CMIC=0,10", "OK", 1500);   // mic gain 0..15 (raise if receiver hears you too quiet)
    sim_at("AT+CLVL=50", "OK", 1500);     // speaker level 0..100 (feeds the reverse ADC)
    sim_at("AT+CSQ", "OK", 1500);
    sim_at("AT+CREG?", "OK", 1500);
    sim_at("AT+CMGDA=\"DEL ALL\"", "OK", 5000);   /* clear old SMS so we don't replay them */

    spk_init();
    adc_init();
    play_q  = xQueueCreate(128, VOICE_FRAME_BYTES);   // node voice  -> decode -> MIC
    voice_q = xQueueCreate(64,  VOICE_FRAME_BYTES);   // SPK ADC     -> LoRa -> node
#if AUDIO_TEST_TONE
    xTaskCreate(tone_task, "tone", 4096, NULL, 5, NULL);
#else
    xTaskCreate(voice_play_task,  "vplay", 40960, NULL, 5, NULL);
    xTaskCreate(adc_capture_task, "vadc",  40960, NULL, 5, NULL);
#endif

    if (lora_init() != ESP_OK) {
        ui_show("LoRa init FAILED", "check wiring", NULL);
        ESP_LOGE(TAG, "LoRa init failed");
    } else {
        ui_show("Listening 433...", NULL, NULL);
    }

    static uint8_t buf[257];
    int len;
    int64_t last_poll = 0;
    int64_t last_fwd_us = 0;                 // when the node last sent us voice
    uint8_t vpkt[1 + VOICE_FRAMES_PER_PKT * VOICE_FRAME_BYTES];
    int vframes = 0;
    vpkt[0] = VOICE_MARKER;
    while (1) {
        // ── reverse audio: during a call, when the node isn't talking, send the
        //    receiver's voice (captured from SPK by the ADC) to the node ──
        bool node_talking = (esp_timer_get_time() - last_fwd_us) < 500000;  // <0.5 s ago
        if (g_in_call && !node_talking) {
            uint8_t frame[VOICE_FRAME_BYTES];
            while (xQueueReceive(voice_q, frame, 0) == pdTRUE) {
                memcpy(&vpkt[1 + vframes * VOICE_FRAME_BYTES], frame, VOICE_FRAME_BYTES);
                if (++vframes >= VOICE_FRAMES_PER_PKT) {
                    lora_send(vpkt, 1 + vframes * VOICE_FRAME_BYTES);
                    vframes = 0;
                }
            }
        } else {
            // node is talking (or no call): drop captured reverse audio, flush tail
            uint8_t junk[VOICE_FRAME_BYTES];
            while (xQueueReceive(voice_q, junk, 0) == pdTRUE) { }
            if (vframes > 0) { lora_send(vpkt, 1 + vframes * VOICE_FRAME_BYTES); vframes = 0; }
        }

        // uplink: LoRa packet from node -> SMS (text) or voice
        if (lora_recv(buf, sizeof(buf) - 1, &len)) {
            // voice packet: [0x01][7-byte frame]... -> queue frames for playback
            if (len > 1 && buf[0] == VOICE_MARKER) {
                last_fwd_us = esp_timer_get_time();     // node is talking -> forward
                int frames = 0;
                for (int i = 1; i + VOICE_FRAME_BYTES <= len; i += VOICE_FRAME_BYTES) {
                    xQueueSend(play_q, &buf[i], 0);
                    frames++;
                }
                static int vpkts = 0;
                if ((vpkts++ % 5) == 0) {
                    ESP_LOGI(TAG, "voice RX: %d frames (pkt #%d, RSSI %d dBm)",
                             frames, vpkts, lora_last_rssi());
                    ui_show("Voice RX (playing)", NULL, NULL);
                }
                continue;
            }

            buf[len] = '\0';
            ESP_LOGI(TAG, "GOT PACKET (%d B): %s", len, (char *)buf);

            // "CALL|<number>" -> place a GSM voice call to the receiver
            if (strncmp((char *)buf, "CALL|", 5) == 0) {
                const char *num = (char *)buf + 5;
                char cmd[40];
                snprintf(cmd, sizeof(cmd), "ATD%.30s;", num);   // ';' = voice call
                ESP_LOGI(TAG, "Dialing %s ...", num);
                ui_show("Calling...", num, NULL);
                sim_at(cmd, "OK", 8000);
                g_in_call = true;              // enable the reverse audio path
            }
            // "ENDCALL" -> hang up
            else if (strncmp((char *)buf, "ENDCALL", 7) == 0) {
                ESP_LOGI(TAG, "Hanging up");
                sim_at("ATH", "OK", 3000);
                ui_show("Call ended", NULL, NULL);
                g_in_call = false;
            }
            // "<phone>|<body>" -> SMS
            else {
                char *bar = strchr((char *)buf, '|');
                if (bar) {
                    *bar = '\0';
                    const char *phone = (char *)buf;
                    const char *body = bar + 1;
                    ESP_LOGI(TAG, "-> SMS to %s: %s", phone, body);
                    ui_show("Sending SMS", phone, body);
                    bool ok = sim_send_sms(phone, body);
                    ui_show(ok ? "SMS SENT" : "SMS FAILED", phone, body);
                } else {
                    ESP_LOGW(TAG, "no '|' separator, ignoring");
                    ui_show("RX bad frame", NULL, (char *)buf);
                }
            }
        }

        // downlink: incoming SMS reply -> LoRa to node (check every ~4 s)
        int64_t now = esp_timer_get_time();
        if (now - last_poll > 4000000) {
            last_poll = now;
            poll_incoming_sms();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
