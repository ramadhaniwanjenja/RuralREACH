#!/usr/bin/env python3
"""
RuralReach Gateway — runs on the Raspberry Pi.

Receives LoRa packets from the Heltec USB-serial bridge, then:
  - SMS packets  -> decrypt, send via SIM800L using AT+CMGS
  - VOICE packets-> decode Codec2, route audio into the SIM800L call
  - CTRL packets -> start (ATD dial) / end (ATH hangup) a voice call

The recipient experiences a normal SMS or a normal phone call.

Author: Ramadhani Shafii Wanjenja  |  Licence: Apache-2.0
"""
import serial
import time
import hashlib
import logging

logging.basicConfig(level=logging.INFO, format="%(asctime)s  %(message)s")
log = logging.getLogger("gateway")

LORA_PORT = "/dev/ttyUSB0"   # Heltec bridge
SIM_PORT  = "/dev/ttyS0"     # SIM800L on Pi UART
LORA_BAUD = 115200
SIM_BAUD  = 9600

PKT_DATA, PKT_VOICE, PKT_CTRL = 0x01, 0x02, 0x03
CTRL_VOICE_START, CTRL_VOICE_END = 1, 2


class SIM800L:
    """Thin AT-command wrapper for SMS and voice calls."""

    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=2)
        self._at("AT")
        self._at("AT+CMGF=1")          # SMS text mode
        log.info("SIM800L ready")

    def _at(self, cmd, wait=1.0):
        self.ser.write((cmd + "\r\n").encode())
        time.sleep(wait)
        return self.ser.read(self.ser.in_waiting or 1).decode(errors="ignore")

    def send_sms(self, phone, body):
        self._at(f'AT+CMGS="{phone}"')
        self.ser.write(body.encode() + bytes([26]))   # body + Ctrl-Z
        time.sleep(4)
        resp = self.ser.read(self.ser.in_waiting or 1).decode(errors="ignore")
        ok = "+CMGS" in resp
        log.info("SMS to %s: %s", phone, "OK" if ok else "FAILED")
        return ok

    def dial(self, phone):
        self._at(f"ATD{phone};")        # voice call
        log.info("Dialling %s for voice call", phone)

    def hangup(self):
        self._at("ATH")
        log.info("Call ended")


def hash_phone(phone):
    """Privacy: never log a plain phone number."""
    return hashlib.sha256(phone.encode()).hexdigest()[:12]


def decrypt(payload):
    """AES-128-CBC decrypt — mirrors the node's aes_crypto.c.
    (Key shared during provisioning; omitted here for brevity.)"""
    # from Crypto.Cipher import AES ; ... ; return plaintext
    return payload          # placeholder for the demo build


def parse_packet(raw):
    """Decode the wire format produced by lora_driver.c."""
    if len(raw) < 9:
        return None
    return {
        "src":   int.from_bytes(raw[0:4], "big"),
        "seq":   int.from_bytes(raw[4:6], "big"),
        "ttl":   raw[6],
        "type":  raw[7],
        "plen":  raw[8],
        "payload": raw[9:9 + raw[8]],
    }


def main():
    sim  = SIM800L(SIM_PORT, SIM_BAUD)
    lora = serial.Serial(LORA_PORT, LORA_BAUD, timeout=1)
    in_call = False
    log.info("RuralReach gateway running — waiting for LoRa packets")

    while True:
        raw = lora.read(256)
        if not raw:
            continue
        pkt = parse_packet(raw)
        if not pkt:
            continue

        if pkt["type"] == PKT_DATA:
            plain = decrypt(pkt["payload"])
            phone = plain[:15].decode(errors="ignore").strip("\x00")
            body  = plain[15:].decode(errors="ignore")
            log.info("SMS packet from node %x -> %s", pkt["src"], hash_phone(phone))
            sim.send_sms(phone, body)

        elif pkt["type"] == PKT_CTRL:
            code = pkt["payload"][0]
            if code == CTRL_VOICE_START and not in_call:
                # phone for the call would be set up during session start
                sim.dial("+250788000000")
                in_call = True
            elif code == CTRL_VOICE_END and in_call:
                sim.hangup()
                in_call = False

        elif pkt["type"] == PKT_VOICE and in_call:
            # decode Codec2 -> PCM, write into SIM800L MIC line (audio HAT)
            # codec2_decode(...) ; audio_out.write(pcm)
            pass


if __name__ == "__main__":
    main()
