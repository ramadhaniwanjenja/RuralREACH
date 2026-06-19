/* SX1278 LoRa driver for the Heltec ESP32 LoRa V3 (433 MHz).
 * Configures the onboard radio over SPI and provides send/recv. */
#include "lora_driver.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lora";
static spi_device_handle_t s_spi;

#define REG_OPMODE 0x01
#define REG_FIFO   0x00
#define MODE_LORA  0x80
#define MODE_TX    0x03
#define MODE_RXC   0x05
#define MODE_STDBY 0x01

static void wr(uint8_t r, uint8_t v) {
    spi_transaction_t t = { .length = 16,
        .tx_data = { r | 0x80, v }, .flags = SPI_TRANS_USE_TXDATA };
    spi_device_polling_transmit(s_spi, &t);
}

esp_err_t lora_driver_init(void)
{
    gpio_set_direction(LORA_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LORA_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));

    spi_bus_config_t bus = { .miso_io_num = LORA_PIN_MISO,
        .mosi_io_num = LORA_PIN_MOSI, .sclk_io_num = LORA_PIN_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 256 };
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = { .clock_speed_hz = 8000000,
        .mode = 0, .spics_io_num = LORA_PIN_CS, .queue_size = 1 };
    spi_bus_add_device(SPI2_HOST, &dev, &s_spi);

    wr(REG_OPMODE, MODE_LORA | MODE_STDBY);
    /* Frf = freq * 2^19 / 32 MHz */
    uint64_t frf = ((uint64_t)LORA_FREQ_HZ << 19) / 32000000ULL;
    wr(0x06, frf >> 16); wr(0x07, frf >> 8); wr(0x08, frf);
    wr(0x1D, 0x72);                  /* BW125, CR4/5                    */
    wr(0x1E, (LORA_SF << 4) | 0x04); /* SF, CRC on                      */
    wr(0x09, 0x8F);                  /* PA boost, ~17 dBm               */
    ESP_LOGI(TAG, "SX1278 ready @ 433 MHz SF%d", LORA_SF);
    return ESP_OK;
}

esp_err_t lora_driver_send(const packet_t *pkt)
{
    uint8_t buf[PACKET_MAX_PAYLOAD + 16];
    int n = 0;
    buf[n++] = pkt->src_node >> 24; buf[n++] = pkt->src_node >> 16;
    buf[n++] = pkt->src_node >> 8;  buf[n++] = pkt->src_node;
    buf[n++] = pkt->seq_num >> 8;   buf[n++] = pkt->seq_num;
    buf[n++] = pkt->ttl; buf[n++] = pkt->type; buf[n++] = pkt->payload_len;
    memcpy(buf + n, pkt->payload, pkt->payload_len); n += pkt->payload_len;
    uint16_t crc = packet_crc16(buf, n);
    buf[n++] = crc >> 8; buf[n++] = crc;

    wr(REG_OPMODE, MODE_LORA | MODE_STDBY);
    wr(0x0D, 0); wr(0x0E, 0);
    for (int i = 0; i < n; i++) wr(REG_FIFO, buf[i]);
    wr(0x22, n);
    wr(REG_OPMODE, MODE_LORA | MODE_TX);
    return ESP_OK;
}

esp_err_t lora_driver_send_ctrl(int ctrl_code)
{
    packet_t p = {0};
    p.type = PKT_TYPE_CTRL; p.payload_len = 1; p.payload[0] = ctrl_code;
    return lora_driver_send(&p);
}

esp_err_t lora_driver_recv(packet_t *pkt, uint32_t timeout_ticks)
{
    wr(REG_OPMODE, MODE_LORA | MODE_RXC);
    /* Full IRQ-driven receive is implemented in the complete firmware.
     * Skeleton returns timeout so the build is clean for the demo. */
    vTaskDelay(timeout_ticks);
    return ESP_ERR_TIMEOUT;
}
