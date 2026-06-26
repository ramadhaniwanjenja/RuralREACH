#pragma once
#include "esp_err.h"
#include "packet.h"

/* Heltec ESP32 LoRa V3 — onboard SX1278 pin map, 433 MHz */
#define LORA_FREQ_HZ   433000000
#define LORA_SF        9          /* SF9 balances range and voice rate */
#define LORA_PIN_SCK   9
#define LORA_PIN_MOSI  10
#define LORA_PIN_MISO  11
#define LORA_PIN_CS    8
#define LORA_PIN_RST   12
#define LORA_PIN_DIO0  14

esp_err_t lora_driver_init(void);
esp_err_t lora_driver_send(const packet_t *pkt);
esp_err_t lora_driver_send_ctrl(int ctrl_code);
esp_err_t lora_driver_recv(packet_t *pkt, uint32_t timeout_ticks);
