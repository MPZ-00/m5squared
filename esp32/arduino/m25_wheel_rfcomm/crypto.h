/**
 * crypto.h - AES-128 ECB / CBC wrappers for M25 packet encryption.
 *
 * The M25 encryption scheme:
 *   Encrypt:  IV (random) -> ECB-encrypt IV -> CBC-encrypt data with original IV
 *   Decrypt:  ECB-decrypt the first 16 bytes to recover IV ->
 *             CBC-decrypt the remaining bytes with recovered IV
 *
 * All functions:
 *   - Accept explicit key pointer (no hidden global)
 *   - Return bool: true = success, false = mbedtls error
 *   - Caller owns all buffers
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>
#include <mbedtls/aes.h>

// ---------------------------------------------------------------------------
// crypto_ecb_encrypt - AES-128 ECB encrypt a single 16-byte block.
// ---------------------------------------------------------------------------
inline bool crypto_ecb_encrypt(const uint8_t key[16],
                                const uint8_t in[16],
                                uint8_t       out[16]) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int rc = mbedtls_aes_setkey_enc(&ctx, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

// ---------------------------------------------------------------------------
// crypto_ecb_decrypt - AES-128 ECB decrypt a single 16-byte block.
// ---------------------------------------------------------------------------
inline bool crypto_ecb_decrypt(const uint8_t key[16],
                                const uint8_t in[16],
                                uint8_t       out[16]) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int rc = mbedtls_aes_setkey_dec(&ctx, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

// ---------------------------------------------------------------------------
// crypto_cbc_decrypt - AES-128 CBC decrypt.
//   iv      : 16-byte IV (not modified; a copy is used internally)
//   in/out  : must be multiples of 16 bytes; len is byte count
// ---------------------------------------------------------------------------
inline bool crypto_cbc_decrypt(const uint8_t key[16],
                                const uint8_t iv[16],
                                const uint8_t* in,
                                size_t         len,
                                uint8_t*       out) {
    if (len == 0 || len % 16 != 0) return false;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    uint8_t ivCopy[16];
    memcpy(ivCopy, iv, 16);   // CBC modifies IV in place; protect caller's copy

    int rc = mbedtls_aes_setkey_dec(&ctx, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                              len, ivCopy, in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

// ---------------------------------------------------------------------------
// crypto_cbc_encrypt - AES-128 CBC encrypt.
//   iv      : 16-byte IV (not modified; a copy is used internally)
// ---------------------------------------------------------------------------
inline bool crypto_cbc_encrypt(const uint8_t key[16],
                                const uint8_t iv[16],
                                const uint8_t* in,
                                size_t         len,
                                uint8_t*       out) {
    if (len == 0 || len % 16 != 0) return false;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    uint8_t ivCopy[16];
    memcpy(ivCopy, iv, 16);

    int rc = mbedtls_aes_setkey_enc(&ctx, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                              len, ivCopy, in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

// ---------------------------------------------------------------------------
// crypto_generate_iv - fill 16 bytes with random data (ESP32 TRNG).
// ---------------------------------------------------------------------------
inline void crypto_generate_iv(uint8_t iv[16]) {
    for (int i = 0; i < 16; i++) {
        iv[i] = (uint8_t)esp_random();
    }
}

#endif // CRYPTO_H
