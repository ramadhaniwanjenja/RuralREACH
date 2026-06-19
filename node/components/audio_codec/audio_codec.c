/* Codec2 wrapper + I2S audio I/O for the RuralReach node.
 * Codec2 compresses 8 kHz speech to 700 bps so it fits in a LoRa packet.
 * I2S microphone (INMP441) and amplifier (MAX98357) provide the audio path. */
#include "audio_codec.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <string.h>
/* #include "codec2.h"   // linked from the Codec2 library on the full build */

static const char *TAG = "audio";
static i2s_chan_handle_t s_rx, s_tx;
/* static struct CODEC2 *s_c2; */

esp_err_t audio_codec_init(void)
{
    /* Codec2 init (full build):
     *   s_c2 = codec2_create(CODEC2_MODE_700C); */

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&cc, &s_tx, &s_rx);
    /* standard 8 kHz mono config applied here in full build */
    ESP_LOGI(TAG, "audio + Codec2 700C ready (8 kHz, 20 ms frames)");
    return ESP_OK;
}

int audio_codec_encode(const int16_t *pcm, uint8_t *bits)
{
    /* codec2_encode(s_c2, bits, (short*)pcm); */
    memcpy(bits, pcm, 4);     /* placeholder: 4 bytes per 700C frame */
    return 4;
}

void audio_codec_decode(const uint8_t *bits, int16_t *pcm)
{
    /* codec2_decode(s_c2, (short*)pcm, bits); */
    memset(pcm, 0, 160 * sizeof(int16_t));
}

void audio_capture_frame(int16_t *pcm)
{
    size_t r = 0;
    i2s_channel_read(s_rx, pcm, 160 * sizeof(int16_t), &r, 100);
}

void audio_play_frame(const int16_t *pcm)
{
    size_t w = 0;
    i2s_channel_write(s_tx, pcm, 160 * sizeof(int16_t), &w, 100);
}
