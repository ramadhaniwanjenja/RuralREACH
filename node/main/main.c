/*
 * RuralReach LoRa Node — main.c
 *
 * Runs on the Heltec ESP32 LoRa V3 (SX1278, 433 MHz). Handles two jobs:
 *   1. SMS: receive a message from the user's phone over Bluetooth,
 *      encrypt it, and transmit over LoRa to the gateway.
 *   2. Voice: while the push-to-talk button is held, sample the I2S
 *      microphone, compress with Codec2, and stream over LoRa.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lora_driver.h"
#include "packet.h"
#include "aes_crypto.h"
#include "audio_codec.h"

static const char *TAG = "node";
#define PTT_GPIO  0          /* push-to-talk button, active low */

/* ── SMS transmit task ────────────────────────────────────────────────
 * In the full build this reads from the Bluetooth SPP queue. Here it is
 * driven by sms_send() which the BT callback calls. */
static QueueHandle_t g_sms_out;

void sms_send(const char *phone, const char *body)
{
    sms_msg_t m = {0};
    strncpy(m.dst_phone, phone, sizeof(m.dst_phone) - 1);
    strncpy(m.body, body, sizeof(m.body) - 1);
    m.body_len = strlen(m.body);
    xQueueSend(g_sms_out, &m, 0);
}

static void task_sms(void *arg)
{
    sms_msg_t m;
    uint16_t seq = 0;
    while (1) {
        if (xQueueReceive(g_sms_out, &m, portMAX_DELAY)) {
            /* Pack phone + body into a plaintext buffer */
            uint8_t plain[160];
            int n = packet_pack_sms(&m, plain, sizeof(plain));

            /* Encrypt */
            uint8_t cipher[192]; size_t clen = 0;
            aes_crypto_encrypt(plain, n, cipher, &clen);

            /* Build and send packet */
            packet_t pkt = {0};
            pkt.src_node = 0xA1B2C3D4;
            pkt.seq_num  = seq++;
            pkt.ttl      = 5;
            pkt.type     = PKT_TYPE_DATA;
            pkt.payload_len = clen;
            memcpy(pkt.payload, cipher, clen);

            lora_driver_send(&pkt);
            ESP_LOGI(TAG, "SMS sent to %s (%d bytes encrypted)", m.dst_phone, (int)clen);
        }
    }
}

/* ── Voice task: only active while PTT is held ──────────────────────── */
static void task_voice(void *arg)
{
    int16_t  pcm[160];                 /* 20 ms @ 8 kHz                   */
    uint8_t  bits[8];                  /* Codec2 700C = 4 bytes / frame  */
    uint16_t vseq = 0;

    while (1) {
        if (gpio_get_level(PTT_GPIO) == 0) {          /* button pressed   */
            ESP_LOGI(TAG, "PTT held — voice session start");
            lora_driver_send_ctrl(CTRL_VOICE_START);

            while (gpio_get_level(PTT_GPIO) == 0) {
                audio_capture_frame(pcm);              /* read I2S mic     */
                int nbits = audio_codec_encode(pcm, bits);

                packet_t v = {0};
                v.src_node = 0xA1B2C3D4;
                v.seq_num  = vseq++;
                v.type     = PKT_TYPE_VOICE;
                v.payload_len = nbits;
                memcpy(v.payload, bits, nbits);
                lora_driver_send(&v);                  /* stream frame     */
            }
            lora_driver_send_ctrl(CTRL_VOICE_END);
            ESP_LOGI(TAG, "PTT released — voice session end");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ── LoRa receive task: incoming voice from the recipient ───────────── */
static void task_rx(void *arg)
{
    packet_t pkt;
    int16_t  pcm[160];
    while (1) {
        if (lora_driver_recv(&pkt, pdMS_TO_TICKS(1000)) == ESP_OK) {
            if (pkt.type == PKT_TYPE_VOICE) {
                audio_codec_decode(pkt.payload, pcm);  /* decompress       */
                audio_play_frame(pcm);                 /* I2S speaker      */
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PTT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);

    ESP_ERROR_CHECK(aes_crypto_init());
    ESP_ERROR_CHECK(lora_driver_init());
    ESP_ERROR_CHECK(audio_codec_init());

    g_sms_out = xQueueCreate(8, sizeof(sms_msg_t));

    xTaskCreate(task_sms,   "sms",   4096, NULL, 5, NULL);
    xTaskCreate(task_voice, "voice", 8192, NULL, 6, NULL);
    xTaskCreate(task_rx,    "rx",    8192, NULL, 6, NULL);

    ESP_LOGI(TAG, "RuralReach node ready — 433 MHz, SMS + PTT voice");
}
