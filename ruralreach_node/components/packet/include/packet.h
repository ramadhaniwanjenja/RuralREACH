#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PACKET_MAX_PAYLOAD 192

typedef enum {
    PKT_TYPE_DATA  = 0x01,   /* SMS payload          */
    PKT_TYPE_VOICE = 0x02,   /* Codec2 voice frame   */
    PKT_TYPE_CTRL  = 0x03,   /* session start/end    */
    PKT_TYPE_ACK   = 0x04,
} packet_type_t;

typedef enum { CTRL_VOICE_START = 1, CTRL_VOICE_END = 2 } ctrl_code_t;

typedef struct {
    uint32_t      src_node;
    uint16_t      seq_num;
    uint8_t       ttl;
    packet_type_t type;
    uint16_t      crc;
    uint8_t       payload_len;
    uint8_t       payload[PACKET_MAX_PAYLOAD];
} packet_t;

typedef struct {
    char    dst_phone[16];
    char    body[160];
    uint8_t body_len;
} sms_msg_t;

uint16_t packet_crc16(const uint8_t *data, size_t len);
int  packet_pack_sms(const sms_msg_t *m, uint8_t *out, size_t cap);
void packet_unpack_sms(const uint8_t *in, size_t len, sms_msg_t *out);
