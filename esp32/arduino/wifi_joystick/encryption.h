#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <mbedtls/aes.h>

// Declared in main sketch
extern const uint8_t encryptionKey[16];

// Encrypt packet using AES-128-ECB
bool encryptPacket(uint8_t* data, size_t len, uint8_t* output) {
    if (len % 16 != 0) {
        Serial.println("Packet size must be multiple of 16");
        return false;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    if (mbedtls_aes_setkey_enc(&aes, encryptionKey, 128) != 0) {
        Serial.println("Failed to set encryption key");
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Encrypt each 16-byte block
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, 
                                   data + i, output + i) != 0) {
            Serial.println("Encryption failed");
            mbedtls_aes_free(&aes);
            return false;
        }
    }
    
    mbedtls_aes_free(&aes);
    return true;
}

#endif
