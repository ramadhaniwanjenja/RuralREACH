#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Codec2 700C: 8 kHz PCM, 20 ms frames (160 samples) -> 4 bytes.
 * I2S mic = INMP441, I2S amp = MAX98357. */
esp_err_t audio_codec_init(void);
int  audio_codec_encode(const int16_t *pcm160, uint8_t *bits_out);  /* returns byte count */
void audio_codec_decode(const uint8_t *bits, int16_t *pcm160_out);
void audio_capture_frame(int16_t *pcm160);   /* read 20 ms from I2S mic */
void audio_play_frame(const int16_t *pcm160);/* write 20 ms to I2S amp  */
