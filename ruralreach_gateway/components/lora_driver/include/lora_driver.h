#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * SX1262 LoRa driver for the ESP32-S3 node (Heltec WiFi LoRa 32 V3 class
 * wiring). The radio is command-based (not register-based like the SX127x)
 * and has a BUSY line plus a TCXO on DIO3 and an RF switch on DIO2.
 *
 * Radio parameters are chosen to match the WaziHat (SX1276) gateway running
 * SX127x at 433 MHz / SF9 / BW125 / CR4-5 / preamble 8 / explicit header /
 * CRC on. The only translation needed is the sync word: SX127x 0x12 (private)
 * == SX126x 0x1424.
 */

#define LORA_FREQ_HZ   433000000u
/* SF7 gives ~4x the throughput of SF9 — needed to carry real-time Codec2 voice
 * without dropping frames. Trades some range for bandwidth. Node and gateway
 * MUST use the same value. */
#define LORA_SF        7

/* Heltec-V3-class boards have a TCXO powered from DIO3. If range is very poor
 * and you suspect your board uses a plain crystal instead, set this to 0.
 * (A wrong TCXO setting detunes the radio and kills range.) */
#define LORA_USE_TCXO  1
#define LORA_TCXO_VOLT 0x02   /* 0x02 = 1.8 V (Heltec V3); 0x07 = 3.3 V */

/* SX1262 SPI + control pins (Heltec V3 layout). */
#define LORA_PIN_SCK   9
#define LORA_PIN_MOSI  10
#define LORA_PIN_MISO  11
#define LORA_PIN_NSS   8
#define LORA_PIN_RST   12
#define LORA_PIN_BUSY  13
#define LORA_PIN_DIO1  14

esp_err_t lora_init(void);
/* Transmit one LoRa packet (blocks until TxDone or ~3 s timeout), then goes
 * back to continuous receive. */
esp_err_t lora_send(const uint8_t *data, size_t len);
/* Put the radio in continuous receive. lora_init() already does this. */
void lora_start_rx(void);
/* Non-blocking: if a valid (good-CRC) packet arrived, copy it into buf, set
 * *out_len, re-arm RX, and return true. Otherwise return false. */
bool lora_recv(uint8_t *buf, size_t cap, int *out_len);
/* RSSI (dBm) of the most recently received packet. */
int lora_last_rssi(void);
