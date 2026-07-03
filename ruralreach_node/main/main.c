/*
 * RuralReach Node — OLED + Audio Loopback + BLE bench test
 *
 * What this firmware lets you test WITHOUT a gateway:
 *   1. Pair the Flutter app over BLE (the node advertises as "RuralReach").
 *   2. Send an SMS from the app  ->  the message is shown on the OLED.
 *   3. Hold the push-to-talk button  ->  mic audio is played straight back
 *      out of the speaker (live loopback) so you hear yourself.
 *
 * The Flutter app talks to the node over a Nordic UART Service (NUS):
 *   it writes lines like "SMS|<phone>|<message>\n" to the RX characteristic.
 *
 * NOTE: the Heltec WiFi LoRa 32 V3 is an ESP32-S3, which has BLE only
 *       (no Classic Bluetooth / SPP), so the link is BLE, not SPP.
 *
 * Pins:
 *   OLED (built-in):  SDA=17  SCL=18  RST=21  Vext=36
 *   INMP441 mic:      SCK=1   WS=2    SD=3
 *   MAX98357A amp:    BCLK=4  LRC=5   DIN=6
 *   PTT button:       GPIO 7  (external, to GND, active-low)  OR
 *                     GPIO 0  (onboard PRG button, also works)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "lora_driver.h"
#include "codec2.h"

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
#define PTT_BTN_EXT GPIO_NUM_7   /* your external hold-to-talk button */
#define PTT_BTN_PRG GPIO_NUM_0   /* onboard PRG button (fallback)     */

#define SAMPLE_RATE 8000     /* Codec2 runs at 8 kHz */
#define BUF_SAMPLES 256

/* Voice-over-LoRa: hold PTT to talk; mic -> Codec2 -> LoRa -> gateway (and the
 * reverse: gateway -> LoRa -> node speaker). Codec2 1300 = 320 samples (40 ms)
 * per frame = 7 bytes. Frames are batched per LoRa packet, prefixed with a
 * 1-byte VOICE marker (0x01) so voice is told apart from ASCII "phone|body". */
#define VOICE_MODE            CODEC2_MODE_1300  /* 1300 bps, clearer than 700C */
#define VOICE_FRAME_BYTES     7                 /* 1300 = 7 bytes/frame (700C = 4) */
#define VOICE_MARKER          0x01
#define VOICE_FRAMES_PER_PKT  12                /* 12 x 40 ms = ~480 ms audio */

/* Recording loudness. Smaller = louder; raise it if the voice distorts/clips. */
#define MIC_GAIN_SHIFT 4
/* Which INMP441 channel to capture:
 *   -1 = AUTO: use whichever channel is louder (= the live mic)  <-- recommended
 *    0 = force LEFT  (INMP441 SEL pin -> GND)
 *    1 = force RIGHT (INMP441 SEL pin -> VDD)
 * Watch the serial line "mic levels  L=..  R=.." to see which one is your mic. */
#define MIC_FORCE_CH   (-1)
/* Hold the button to RECORD, release to PLAY BACK. Doing it this way (instead
 * of a live mic->speaker loopback) makes acoustic feedback impossible, so you
 * hear your actual words. This many seconds of audio are buffered. */
#define REC_SECONDS    4
#define REC_MAX        (SAMPLE_RATE * REC_SECONDS)

/* UI modes for ui_draw(). */
#define UI_IDLE 0
#define UI_REC  1
#define UI_PLAY 2

static const char *TAG = "node";
static i2c_master_dev_handle_t oled;
static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;
static uint8_t fb[OLED_W * OLED_H / 8];
static QueueHandle_t voice_q;      /* Codec2 frames: capture task -> main loop (TX) */
static QueueHandle_t play_q;       /* Codec2 frames: main loop (RX) -> decode task */

/* ── Shared state between the BLE host task and the audio/UI loop ───── */
static SemaphoreHandle_t   state_lock;
static char                g_msg[84]   = "";   /* last message from the app */
static volatile bool       g_msg_dirty = true; /* UI needs a redraw         */
static volatile bool       g_ble_connected = false;
static volatile bool       g_app_ptt   = false; /* app-driven push-to-talk  */
static volatile uint16_t   g_conn_handle = 0;
static uint16_t            g_tx_val_handle = 0;
static uint8_t             g_own_addr_type = 0;

/* LoRa forward: BLE callback stashes "phone|body" here; the main loop sends it
 * over LoRa (the BLE host task must not block on a ~150 ms radio transmit). */
static char                g_tx_buf[180] = "";
static volatile bool       g_tx_pending  = false;
static volatile bool       g_lora_sent   = false;
static volatile bool       g_is_reply    = false;  /* last g_msg is a LoRa reply */

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
/* Word-wrap a string across pages, ~21 chars per line. */
static void oled_draw_wrapped(int page, int max_pages, const char *s) {
    char line[22];
    int used = 0;
    while (*s && used < max_pages) {
        int n = 0;
        while (s[n] && s[n] != '\n' && n < 21) n++;
        memcpy(line, s, n);
        line[n] = '\0';
        oled_draw_string(0, page + used, line);
        s += n;
        if (*s == '\n') s++;
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

// ── Mic + speaker init ───────────────────────────────────────────────
static void mic_init(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&cc, NULL, &rx_handle);
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        /* Read BOTH channels: the INMP441 drives only one of them, and we
         * auto-detect which in the loopback loop (see app_main). */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
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
    cc.auto_clear = true;   // output silence (not stale audio) between voice packets
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
        .pin_bit_mask = (1ULL << PTT_BTN_EXT) | (1ULL << PTT_BTN_PRG),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "PTT buttons ready (GPIO7 external, GPIO0 onboard)");
}

/* Pressed if the external button, the onboard PRG, or the app says so. */
static bool ptt_pressed(void) {
    return gpio_get_level(PTT_BTN_EXT) == 0
        || gpio_get_level(PTT_BTN_PRG) == 0
        || g_app_ptt;
}

// ── BLE: parse a line from the app and stash it for the UI ───────────
static void handle_line(const char *line) {
    ESP_LOGI(TAG, "RX line: %s", line);

    if (strncmp(line, "PTT_START", 9) == 0) { g_app_ptt = true;  return; }
    if (strncmp(line, "PTT_END",   7) == 0) { g_app_ptt = false; return; }

    /* "CALL|<number>" / "ENDCALL" -> forward verbatim to the gateway, which
     * places/ends a GSM voice call to the receiver. */
    if (strncmp(line, "CALL|", 5) == 0 || strncmp(line, "ENDCALL", 7) == 0) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        strncpy(g_tx_buf, line, sizeof(g_tx_buf) - 1);
        g_tx_buf[sizeof(g_tx_buf) - 1] = '\0';
        g_tx_pending = true;
        xSemaphoreGive(state_lock);
        return;
    }

    /* "SMS|<phone>|<message>" -> show phone on one line, message below, and
     * queue "<phone>|<message>" for the LoRa forward to the gateway. */
    char shown[84];
    bool is_sms = false;
    if (strncmp(line, "SMS|", 4) == 0) {
        is_sms = true;
        const char *phone = line + 4;            /* "phone|body" */
        const char *sep = strchr(phone, '|');
        if (sep) {
            int plen = (int)(sep - phone);
            if (plen > 20) plen = 20;
            snprintf(shown, sizeof(shown), "To:%.*s\n%s", plen, phone, sep + 1);
        } else {
            snprintf(shown, sizeof(shown), "%s", phone);
        }
    } else {
        snprintf(shown, sizeof(shown), "%s", line);
    }

    xSemaphoreTake(state_lock, portMAX_DELAY);
    strncpy(g_msg, shown, sizeof(g_msg) - 1);
    g_msg[sizeof(g_msg) - 1] = '\0';
    g_msg_dirty = true;
    g_lora_sent = false;
    g_is_reply  = false;
    if (is_sms) {
        strncpy(g_tx_buf, line + 4, sizeof(g_tx_buf) - 1);  /* "phone|body" */
        g_tx_buf[sizeof(g_tx_buf) - 1] = '\0';
        g_tx_pending = true;
    }
    xSemaphoreGive(state_lock);
}

// ── BLE: Nordic UART Service GATT server ─────────────────────────────
/* Service 6E400001..., RX (write) 6E400002..., TX (notify) 6E400003... */
static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
                     0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t nus_rx_uuid =
    BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
                     0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t nus_tx_uuid =
    BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
                     0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);

static int nus_rx_access(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[128];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
    buf[len] = '\0';

    /* The app may send several "\n"-terminated lines in one packet. */
    char *start = buf;
    for (char *p = buf; ; p++) {
        if (*p == '\n' || *p == '\0') {
            char saved = *p;
            *p = '\0';
            if (p > start) handle_line(start);
            start = p + 1;
            if (saved == '\0') break;
        }
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &nus_rx_uuid.u,
                .access_cb = nus_rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &nus_tx_uuid.u,
                .access_cb = nus_rx_access, /* TX is notify-only; cb unused */
                .val_handle = &g_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

static void ble_advertise(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_ble_connected = true;
            g_conn_handle = event->connect.conn_handle;
        } else {
            ble_advertise();
        }
        g_msg_dirty = true;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        g_ble_connected = false;
        g_app_ptt = false;
        g_msg_dirty = true;
        ble_advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void ble_advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                      &adv, ble_gap_event, NULL);
    ESP_LOGI(TAG, "BLE advertising as \"%s\"", name);
}

static void ble_on_sync(void) {
    ble_hs_id_infer_auto(0, &g_own_addr_type);
    ble_advertise();
}

static void ble_host_task(void *param) {
    nimble_port_run();              /* returns only on nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void ble_init(void) {
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));
    ble_svc_gap_device_name_set("RuralReach");

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE (NimBLE) started");
}

/* Push a text notification to the paired phone on the NUS TX characteristic.
 * No-op if nothing is connected/subscribed. */
static void ble_notify_text(const char *s) {
    if (!g_ble_connected || g_tx_val_handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(s, strlen(s));
    if (om) ble_gatts_notify_custom(g_conn_handle, g_tx_val_handle, om);
}

// ── UI: redraw whole status screen ───────────────────────────────────
static void ui_draw(int mode, int counter) {
    oled_clear();
    oled_draw_string(0, 0, "RuralReach Node");
    oled_draw_string(0, 1, g_ble_connected ? "BLE: connected" : "BLE: advertising");

    if (mode == UI_REC) {
        oled_draw_string(0, 3, " >> RECORDING <<");
        char line[24];
        snprintf(line, sizeof(line), "Samples: %d", counter);
        oled_draw_string(0, 5, line);
        oled_draw_string(0, 7, "Speak now...");
    } else if (mode == UI_PLAY) {
        oled_draw_string(0, 3, "  >> PLAYING <<");
        char line[24];
        snprintf(line, sizeof(line), "Samples: %d", counter);
        oled_draw_string(0, 5, line);
        oled_draw_string(0, 7, "Listen!");
    } else {
        char msg[84];
        xSemaphoreTake(state_lock, portMAX_DELAY);
        strncpy(msg, g_msg, sizeof(msg));
        msg[sizeof(msg) - 1] = '\0';
        xSemaphoreGive(state_lock);
        if (msg[0]) {
            oled_draw_string(0, 3, g_is_reply ? "Reply from gateway:" : "Message:");
            oled_draw_wrapped(4, 3, msg);
        } else {
            oled_draw_string(0, 4, "No messages yet");
        }
        oled_draw_string(0, 7, g_lora_sent ? "-> sent via LoRa" : "Hold=rec, rel=play");
    }
    oled_flush();
}

// ── Voice capture task: while PTT held, mic -> Codec2 -> voice_q ──────
// Runs in its own big-stack task (Codec2 encode needs ~32 KB). It only
// produces encoded frames; the main loop owns the LoRa radio and sends them.
static void voice_task(void *arg) {
    struct CODEC2 *c2 = codec2_create(VOICE_MODE);
    if (!c2) { ESP_LOGE(TAG, "codec2_create failed"); vTaskDelete(NULL); return; }
    int nsam = codec2_samples_per_frame(c2);    // 320
    ESP_LOGI(TAG, "Voice TX ready (Codec2 1300, %d samples/frame, %d bytes)",
             nsam, codec2_bytes_per_frame(c2));

    static int32_t raw[320 * 2];                 // stereo 32-bit from INMP441
    static int16_t pcm[320];
    static uint8_t bits[16];
    int32_t dc = 0;

    while (1) {
        if (!ptt_pressed()) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        // read exactly one frame worth of stereo samples
        size_t need = (size_t)nsam * 2 * sizeof(int32_t), got = 0;
        while (got < need && ptt_pressed()) {
            size_t r = 0;
            i2s_channel_read(rx_handle, (uint8_t *)raw + got, need - got,
                             &r, pdMS_TO_TICKS(100));
            got += r;
        }
        if (got < need) continue;                // released mid-frame

        // pick the live INMP441 channel, remove DC, apply gain -> mono 16-bit
        int32_t lpk = 0, rpk = 0;
        for (int i = 0; i < nsam; i++) {
            int32_t l = raw[2 * i] >> 8, rr = raw[2 * i + 1] >> 8;
            int32_t la = l < 0 ? -l : l, ra = rr < 0 ? -rr : rr;
            if (la > lpk) lpk = la;
            if (ra > rpk) rpk = ra;
        }
        int ch = (MIC_FORCE_CH >= 0) ? MIC_FORCE_CH : (lpk >= rpk ? 0 : 1);
        for (int i = 0; i < nsam; i++) {
            int32_t v = raw[2 * i + ch] >> 8;
            dc += (v - dc) >> 10;
            int32_t s = (v - dc) >> MIC_GAIN_SHIFT;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            pcm[i] = (int16_t)s;
        }

        codec2_encode(c2, bits, pcm);            // 320 samples -> 7 bytes
        xQueueSend(voice_q, bits, 0);            // drop if the queue is full
    }
}

// ── Voice playback task: play_q (Codec2 frames) -> decode -> speaker ──
// Plays voice received from the gateway (reverse direction). Big stack for
// Codec2 decode.
static void voice_play_task(void *arg) {
    struct CODEC2 *c2 = codec2_create(VOICE_MODE);
    if (!c2) { ESP_LOGE(TAG, "codec2_create (play) failed"); vTaskDelete(NULL); return; }
    int nsam = codec2_samples_per_frame(c2);
    ESP_LOGI(TAG, "Voice RX ready (decode -> speaker)");
    static int16_t pcm[320];
    uint8_t frame[VOICE_FRAME_BYTES];
    size_t wrote;
    while (1) {
        if (xQueueReceive(play_q, frame, portMAX_DELAY) == pdTRUE) {
            codec2_decode(c2, pcm, frame);
            i2s_channel_write(tx_handle, pcm, nsam * sizeof(int16_t),
                              &wrote, pdMS_TO_TICKS(200));
        }
    }
}

// ── Main ─────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "RuralReach booting...");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    state_lock = xSemaphoreCreateMutex();

    oled_init();
    button_init();
    mic_init();
    spk_init();
    ble_init();
    if (lora_init() != ESP_OK) ESP_LOGE(TAG, "LoRa init failed");

    voice_q = xQueueCreate(64, VOICE_FRAME_BYTES);   // mic capture -> TX
    play_q  = xQueueCreate(128, VOICE_FRAME_BYTES);  // RX -> decode/play
    xTaskCreate(voice_task,      "voicetx", 40960, NULL, 5, NULL);
    xTaskCreate(voice_play_task, "voicerx", 40960, NULL, 5, NULL);

    ui_draw(UI_IDLE, 0);

    bool was_pressed = false;
    uint8_t vpkt[1 + VOICE_FRAMES_PER_PKT * VOICE_FRAME_BYTES];
    int vframes = 0;
    vpkt[0] = VOICE_MARKER;

    while (1) {
        bool pressed = ptt_pressed();

        if (pressed) {
            // ── talking: batch Codec2 frames from the capture task, send over LoRa ──
            if (!was_pressed) { ESP_LOGI(TAG, ">> voice TX"); ui_draw(UI_REC, 0); }
            uint8_t frame[VOICE_FRAME_BYTES];
            while (xQueueReceive(voice_q, frame, 0) == pdTRUE) {
                memcpy(&vpkt[1 + vframes * VOICE_FRAME_BYTES], frame, VOICE_FRAME_BYTES);
                if (++vframes >= VOICE_FRAMES_PER_PKT) {
                    lora_send(vpkt, 1 + vframes * VOICE_FRAME_BYTES);
                    vframes = 0;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            // ── just released: flush remaining frames, then go idle ──
            if (was_pressed) {
                uint8_t frame[VOICE_FRAME_BYTES];
                while (xQueueReceive(voice_q, frame, 0) == pdTRUE) {
                    memcpy(&vpkt[1 + vframes * VOICE_FRAME_BYTES], frame, VOICE_FRAME_BYTES);
                    if (++vframes >= VOICE_FRAMES_PER_PKT) {
                        lora_send(vpkt, 1 + vframes * VOICE_FRAME_BYTES);
                        vframes = 0;
                    }
                }
                if (vframes > 0) { lora_send(vpkt, 1 + vframes * VOICE_FRAME_BYTES); vframes = 0; }
                ESP_LOGI(TAG, "<< voice end");
                ui_draw(UI_IDLE, 0);
            }

            // refresh idle screen when a new BLE message arrives
            if (g_msg_dirty) { g_msg_dirty = false; ui_draw(UI_IDLE, 0); }

            // forward a queued SMS over LoRa
            if (g_tx_pending) {
                char tx[180];
                xSemaphoreTake(state_lock, portMAX_DELAY);
                strncpy(tx, g_tx_buf, sizeof(tx));
                tx[sizeof(tx) - 1] = '\0';
                g_tx_pending = false;
                xSemaphoreGive(state_lock);
                ESP_LOGI(TAG, "LoRa TX -> gateway: \"%s\"", tx);
                if (lora_send((const uint8_t *)tx, strlen(tx)) == ESP_OK) g_lora_sent = true;
                g_msg_dirty = true;
            }

            // receive from the gateway: voice -> play on speaker, or SMS reply
            static uint8_t lrx[257];
            int llen;
            if (lora_recv(lrx, sizeof(lrx) - 1, &llen)) {
                if (llen > 1 && lrx[0] == VOICE_MARKER) {
                    for (int i = 1; i + VOICE_FRAME_BYTES <= llen; i += VOICE_FRAME_BYTES)
                        xQueueSend(play_q, &lrx[i], 0);
                } else {
                    lrx[llen] = '\0';
                    ESP_LOGI(TAG, "LoRa RX reply: %s", (char *)lrx);
                    ble_notify_text((char *)lrx);
                    char shown[84];
                    char *bar = strchr((char *)lrx, '|');
                    if (bar) {
                        *bar = '\0';
                        snprintf(shown, sizeof(shown), "%.24s:\n%.50s", (char *)lrx, bar + 1);
                    } else {
                        snprintf(shown, sizeof(shown), "%.80s", (char *)lrx);
                    }
                    xSemaphoreTake(state_lock, portMAX_DELAY);
                    strncpy(g_msg, shown, sizeof(g_msg) - 1);
                    g_msg[sizeof(g_msg) - 1] = '\0';
                    g_msg_dirty = true;
                    g_is_reply  = true;
                    xSemaphoreGive(state_lock);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        was_pressed = pressed;
    }
}
