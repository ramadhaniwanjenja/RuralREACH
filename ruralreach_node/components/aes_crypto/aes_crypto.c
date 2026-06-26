#include "aes_crypto.h"
#include "mbedtls/aes.h"
#include "esp_random.h"
#include "nvs.h"
#include <string.h>

#define KEYLEN 16
#define IVLEN  16
static uint8_t s_key[KEYLEN];

esp_err_t aes_crypto_init(void)
{
    nvs_handle_t h;
    if (nvs_open("rrkey", NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    size_t sz = KEYLEN;
    if (nvs_get_blob(h, "psk", s_key, &sz) != ESP_OK) {
        esp_fill_random(s_key, KEYLEN);          /* first boot: generate   */
        nvs_set_blob(h, "psk", s_key, KEYLEN);
        nvs_commit(h);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t aes_crypto_encrypt(const uint8_t *in, size_t len, uint8_t *out, size_t *out_len)
{
    size_t pad = 16 - (len % 16), tot = len + pad;
    uint8_t buf[176];
    if (tot > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, in, len);
    memset(buf + len, (uint8_t)pad, pad);

    esp_fill_random(out, IVLEN);                 /* random IV prepended    */
    uint8_t iv[IVLEN]; memcpy(iv, out, IVLEN);

    mbedtls_aes_context c; mbedtls_aes_init(&c);
    mbedtls_aes_setkey_enc(&c, s_key, 128);
    mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_ENCRYPT, tot, iv, buf, out + IVLEN);
    mbedtls_aes_free(&c);
    *out_len = IVLEN + tot;
    return ESP_OK;
}

esp_err_t aes_crypto_decrypt(const uint8_t *in, size_t len, uint8_t *out, size_t *out_len)
{
    if (len <= IVLEN) return ESP_ERR_INVALID_SIZE;
    uint8_t iv[IVLEN]; memcpy(iv, in, IVLEN);
    size_t clen = len - IVLEN;

    mbedtls_aes_context c; mbedtls_aes_init(&c);
    mbedtls_aes_setkey_dec(&c, s_key, 128);
    mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_DECRYPT, clen, iv, in + IVLEN, out);
    mbedtls_aes_free(&c);

    uint8_t pad = out[clen - 1];
    *out_len = (pad && pad <= 16) ? clen - pad : clen;
    return ESP_OK;
}
