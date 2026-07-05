/*
 * SX1262 LoRa driver for the ESP32-S3 node (Heltec WiFi LoRa 32 V3 class).
 * Command-based SPI interface. Configures TCXO (DIO3), RF switch (DIO2),
 * 433 MHz / SF9 / BW125 / CR4-5 / explicit header / CRC, sync word 0x1424
 * (matches the SX127x gateway's 0x12), then sends packets.
 */
#include "lora_driver.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lora";
static spi_device_handle_t s_spi;
static int s_last_rssi = 0;    /* dBm of the last received packet */

int lora_last_rssi(void) { return s_last_rssi; }

/* ── SX1262 opcodes ─────────────────────────────────────────────────── */
#define OP_SET_STANDBY        0x80
#define OP_SET_PACKET_TYPE    0x8A
#define OP_SET_RF_FREQ        0x86
#define OP_SET_PA_CONFIG      0x95
#define OP_SET_TX_PARAMS      0x8E
#define OP_SET_MODULATION     0x8B
#define OP_SET_PACKET_PARAMS  0x8C
#define OP_SET_BUF_BASE       0x8F
#define OP_WRITE_BUFFER       0x0E
#define OP_WRITE_REGISTER     0x0D
#define OP_SET_DIO_IRQ        0x08
#define OP_GET_IRQ_STATUS     0x12
#define OP_CLEAR_IRQ_STATUS   0x02
#define OP_SET_TX             0x83
#define OP_SET_RX             0x82
#define OP_GET_RX_BUF_STATUS  0x13
#define OP_GET_PKT_STATUS     0x14
#define OP_READ_BUFFER        0x1E
#define OP_SET_REGULATOR      0x96
#define OP_CALIBRATE          0x89
#define OP_CALIBRATE_IMAGE    0x98
#define OP_SET_DIO3_TCXO      0x97
#define OP_SET_DIO2_RFSW      0x9D

#define STDBY_RC              0x00
#define PACKET_TYPE_LORA      0x01
#define IRQ_TX_DONE           0x0001
#define IRQ_RX_DONE           0x0002
#define IRQ_CRC_ERR           0x0040
#define IRQ_TIMEOUT           0x0200

#define REG_LORA_SYNCWORD_MSB 0x0740   /* 0x1424 == SX127x private 0x12 */

static bool busy_wait(void)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(LORA_PIN_BUSY)) {
        if (esp_timer_get_time() - t0 > 100000) {   /* 100 ms */
            ESP_LOGE(TAG, "BUSY stuck high");
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

/* One SPI transaction (CS framed automatically by the driver). */
static void xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void cmd(uint8_t op, const uint8_t *params, size_t n)
{
    uint8_t buf[260];
    buf[0] = op;
    if (n) memcpy(buf + 1, params, n);
    busy_wait();
    xfer(buf, NULL, n + 1);
}

static void write_reg(uint16_t addr, uint8_t val)
{
    uint8_t p[3] = { addr >> 8, addr & 0xFF, val };
    cmd(OP_WRITE_REGISTER, p, 3);
}

static uint16_t get_irq_status(void)
{
    uint8_t tx[4] = { OP_GET_IRQ_STATUS, 0, 0, 0 };
    uint8_t rx[4] = { 0 };
    busy_wait();
    xfer(tx, rx, 4);
    return ((uint16_t)rx[2] << 8) | rx[3];
}

esp_err_t lora_init(void)
{
    /* BUSY input, RST output */
    gpio_config_t in = { .pin_bit_mask = 1ULL << LORA_PIN_BUSY, .mode = GPIO_MODE_INPUT };
    gpio_config(&in);
    gpio_set_direction(LORA_PIN_RST, GPIO_MODE_OUTPUT);

    /* hardware reset */
    gpio_set_level(LORA_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(LORA_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));

    spi_bus_config_t bus = {
        .miso_io_num = LORA_PIN_MISO, .mosi_io_num = LORA_PIN_MOSI,
        .sclk_io_num = LORA_PIN_SCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 256,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2000000, .mode = 0,
        .spics_io_num = LORA_PIN_NSS, .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) return err;

    busy_wait();
    uint8_t p[8];

    p[0] = STDBY_RC;                       cmd(OP_SET_STANDBY, p, 1);
    p[0] = 0x01;                           cmd(OP_SET_REGULATOR, p, 1);   /* DC-DC */

#if LORA_USE_TCXO
    /* TCXO on DIO3, ~5 ms startup (timeout in 15.625 us steps) */
    p[0] = LORA_TCXO_VOLT; p[1] = 0x00; p[2] = 0x01; p[3] = 0x40;
    cmd(OP_SET_DIO3_TCXO, p, 4);
#endif
    p[0] = 0x7F;                           cmd(OP_CALIBRATE, p, 1);       /* all blocks */
    vTaskDelay(pdMS_TO_TICKS(5));
    busy_wait();

    p[0] = 0x01;                           cmd(OP_SET_DIO2_RFSW, p, 1);   /* DIO2 = RF switch */
    p[0] = PACKET_TYPE_LORA;               cmd(OP_SET_PACKET_TYPE, p, 1);

    /* RF frequency: freq * 2^25 / 32 MHz */
    uint32_t frf = (uint32_t)(((uint64_t)LORA_FREQ_HZ << 25) / 32000000ULL);
    p[0] = frf >> 24; p[1] = frf >> 16; p[2] = frf >> 8; p[3] = frf;
    cmd(OP_SET_RF_FREQ, p, 4);
    p[0] = 0x6B; p[1] = 0x6F;              cmd(OP_CALIBRATE_IMAGE, p, 2); /* 430-440 MHz */

    /* PA config + TX power (22 dBm) */
    p[0] = 0x04; p[1] = 0x07; p[2] = 0x00; p[3] = 0x01;
    cmd(OP_SET_PA_CONFIG, p, 4);
    p[0] = 0x16; p[1] = 0x04;              cmd(OP_SET_TX_PARAMS, p, 2);   /* 22 dBm, 200us ramp */

    /* Modulation: SF9, BW125 (0x04), CR 4/5 (0x01), LDRO off */
    p[0] = LORA_SF; p[1] = 0x04; p[2] = 0x01; p[3] = 0x00;
    cmd(OP_SET_MODULATION, p, 4);

    /* Sync word 0x1424 (== SX127x private 0x12) */
    write_reg(REG_LORA_SYNCWORD_MSB,     0x14);
    write_reg(REG_LORA_SYNCWORD_MSB + 1, 0x24);

    p[0] = 0x00; p[1] = 0x00;              cmd(OP_SET_BUF_BASE, p, 2);

    ESP_LOGI(TAG, "SX1262 ready @ %u Hz SF%d BW125 sync=0x1424",
             (unsigned)LORA_FREQ_HZ, LORA_SF);
    lora_start_rx();                       /* gateway listens by default */
    return ESP_OK;
}

esp_err_t lora_send(const uint8_t *data, size_t len)
{
    if (len == 0 || len > 250) return ESP_ERR_INVALID_ARG;

    uint8_t p[8];
    p[0] = STDBY_RC;                       cmd(OP_SET_STANDBY, p, 1);

    /* write payload to buffer offset 0 */
    uint8_t wb[2 + 250];
    wb[0] = OP_WRITE_BUFFER; wb[1] = 0x00;
    memcpy(wb + 2, data, len);
    busy_wait();
    xfer(wb, NULL, len + 2);

    /* packet params: preamble 8, explicit header, payload=len, CRC on, std IQ */
    p[0] = 0x00; p[1] = 0x08; p[2] = 0x00; p[3] = (uint8_t)len;
    p[4] = 0x01; p[5] = 0x00;
    cmd(OP_SET_PACKET_PARAMS, p, 6);

    /* route TxDone + Timeout to IRQ and DIO1 */
    uint16_t mask = IRQ_TX_DONE | IRQ_TIMEOUT;
    p[0] = mask >> 8; p[1] = mask; p[2] = mask >> 8; p[3] = mask;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0;
    cmd(OP_SET_DIO_IRQ, p, 8);

    p[0] = 0xFF; p[1] = 0xFF;              cmd(OP_CLEAR_IRQ_STATUS, p, 2);

    /* SetTx, timeout 0 = no radio timeout, we poll instead */
    p[0] = 0x00; p[1] = 0x00; p[2] = 0x00; cmd(OP_SET_TX, p, 3);

    int64_t t0 = esp_timer_get_time();
    while (1) {
        uint16_t irq = get_irq_status();
        if (irq & IRQ_TX_DONE) break;
        if (irq & IRQ_TIMEOUT) { ESP_LOGE(TAG, "TX timeout (radio)"); return ESP_FAIL; }
        if (esp_timer_get_time() - t0 > 3000000) { ESP_LOGE(TAG, "TX timeout (poll)"); return ESP_FAIL; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    p[0] = 0xFF; p[1] = 0xFF;              cmd(OP_CLEAR_IRQ_STATUS, p, 2);
    ESP_LOGI(TAG, "TX done (%d bytes)", (int)len);
    lora_start_rx();                       /* return to listening */
    return ESP_OK;
}

void lora_start_rx(void)
{
    uint8_t p[8];
    p[0] = STDBY_RC;                       cmd(OP_SET_STANDBY, p, 1);

    /* packet params: preamble 8, explicit header, max payload 255, CRC on, std IQ */
    p[0] = 0x00; p[1] = 0x08; p[2] = 0x00; p[3] = 0xFF; p[4] = 0x01; p[5] = 0x00;
    cmd(OP_SET_PACKET_PARAMS, p, 6);

    uint16_t mask = IRQ_RX_DONE | IRQ_CRC_ERR | IRQ_TIMEOUT;
    p[0] = mask >> 8; p[1] = mask; p[2] = mask >> 8; p[3] = mask;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0;
    cmd(OP_SET_DIO_IRQ, p, 8);
    p[0] = 0xFF; p[1] = 0xFF;              cmd(OP_CLEAR_IRQ_STATUS, p, 2);

    /* SetRx with timeout 0xFFFFFF = continuous receive */
    p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF; cmd(OP_SET_RX, p, 3);
}

bool lora_recv(uint8_t *buf, size_t cap, int *out_len)
{
    uint16_t irq = get_irq_status();
    if (!(irq & IRQ_RX_DONE)) return false;

    uint8_t p[3] = { 0xFF, 0xFF, 0 };
    cmd(OP_CLEAR_IRQ_STATUS, p, 2);
    if (irq & IRQ_CRC_ERR) { lora_start_rx(); return false; }  /* corrupt */

    /* GetRxBufferStatus -> payload length + start offset */
    uint8_t tx[4] = { OP_GET_RX_BUF_STATUS, 0, 0, 0 };
    uint8_t rx[4] = { 0 };
    busy_wait();
    xfer(tx, rx, 4);
    int len = rx[2];
    int start = rx[3];
    if (len > (int)cap) len = (int)cap;

    /* ReadBuffer(start, len): opcode, offset, NOP, then data */
    static uint8_t rbuf[3 + 256];
    static uint8_t rout[3 + 256];
    rbuf[0] = OP_READ_BUFFER; rbuf[1] = (uint8_t)start; rbuf[2] = 0;
    memset(rbuf + 3, 0, len);
    busy_wait();
    xfer(rbuf, rout, 3 + len);
    memcpy(buf, rout + 3, len);
    *out_len = len;

    /* GetPacketStatus -> RSSI of this packet (rssi_dbm = -rssiPkt/2) */
    uint8_t st_tx[4] = { OP_GET_PKT_STATUS, 0, 0, 0 };
    uint8_t st_rx[4] = { 0 };
    busy_wait();
    xfer(st_tx, st_rx, 4);
    s_last_rssi = -((int)st_rx[2]) / 2;

    lora_start_rx();                       /* re-arm */
    return true;
}
