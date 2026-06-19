#include "packet.h"
#include <string.h>

/* CRC-16/CCITT, poly 0x1021, init 0xFFFF */
uint16_t packet_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

int packet_pack_sms(const sms_msg_t *m, uint8_t *out, size_t cap)
{
    if (cap < 16u + m->body_len) return -1;
    memset(out, 0, 16);
    memcpy(out, m->dst_phone, 15);
    memcpy(out + 15, m->body, m->body_len);
    return 15 + m->body_len;
}

void packet_unpack_sms(const uint8_t *in, size_t len, sms_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->dst_phone, in, 15);
    size_t blen = len - 15;
    if (blen > sizeof(out->body) - 1) blen = sizeof(out->body) - 1;
    memcpy(out->body, in + 15, blen);
    out->body_len = blen;
}
