#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t aes_crypto_init(void);
esp_err_t aes_crypto_encrypt(const uint8_t *in, size_t len, uint8_t *out, size_t *out_len);
esp_err_t aes_crypto_decrypt(const uint8_t *in, size_t len, uint8_t *out, size_t *out_len);
