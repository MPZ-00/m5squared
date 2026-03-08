/**
 * packet.h - Higher-level M25 packet codec.
 *
 * Sits between protocol.h (framing) and command.h (logic).
 *
 *  Decode path:  stuffed wire bytes
 *                  -> proto_frame_parse  (framing / CRC)
 *                  -> crypto_*           (AES-CBC decrypt, PKCS7 strip)
 *                  -> raw SPP bytes
 *
 *  Encode path:  raw SPP bytes (ACK)
 *                  -> PKCS7 pad
 *                  -> crypto_*           (AES-CBC encrypt, ECB-encrypt IV)
 *                  -> proto_frame_build  (framing / CRC / stuffing)
 *                  -> stuffed wire bytes
 */

#ifndef PACKET_H
#define PACKET_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "crypto.h"
#include "state.h"

// SPP ACK packet layout (9 bytes)
#define SPP_ACK_PROTOCOL_ID  0x23
#define SPP_ACK_TELEGRAM_ID  0x01
#define SPP_ACK_SRC          0x10   // Source: wheel
#define SPP_ACK_DST          0x01   // Destination: remote
#define SPP_ACK_SERVICE      0x01   // APP_MGMT
#define SPP_ACK_PARAM        0xFF   // ACK param

// ---------------------------------------------------------------------------
// packet_decode - decrypt a raw M25 frame into its SPP payload.
//
//   raw / rawLen  : stuffed wire bytes as received
//   key           : 16-byte encryption key
//   spp           : output buffer for decrypted SPP bytes (caller: >=32 bytes)
//   sppLen        : number of valid SPP bytes written
//
//   Returns true on success (CRC valid, decryption OK, PKCS7 OK).
// ---------------------------------------------------------------------------
inline bool packet_decode(const uint8_t* raw, size_t rawLen,
                           const uint8_t  key[16],
                           uint8_t* spp, size_t* sppLen) {
    // Step 1: Frame parsing + CRC validation
    uint8_t payload[128];
    size_t  payloadLen = 0;
    if (!proto_frame_parse(raw, rawLen, payload, &payloadLen)) return false;

    // Step 2: Must be at least IV(16) + one data block(16)
    if (payloadLen < 32 || payloadLen % 16 != 0) return false;

    // Step 3: ECB-decrypt the first 16 bytes to recover the IV
    uint8_t iv[16];
    if (!crypto_ecb_decrypt(key, payload, iv)) return false;

    // Step 4: CBC-decrypt remaining bytes using the recovered IV
    const uint8_t* encData    = payload + 16;
    const size_t   encDataLen = payloadLen - 16;
    uint8_t        plain[64];
    if (!crypto_cbc_decrypt(key, iv, encData, encDataLen, plain)) return false;

    // Step 5: Strip PKCS7 padding
    const uint8_t padByte = plain[encDataLen - 1];
    if (padByte == 0 || padByte > 16 || padByte > encDataLen) {
        // Malformed padding: treat as no padding
        *sppLen = encDataLen;
    } else {
        *sppLen = encDataLen - padByte;
    }

    memcpy(spp, plain, *sppLen);
    return true;
}

// ---------------------------------------------------------------------------
// packet_encode_ack - build a stuffed M25 frame carrying the ACK response.
//
//   s    : current wheel state (battery, assist, profile go into ACK)
//   key  : 16-byte encryption key
//   out  : output buffer (caller: >=128 bytes)
//
//   Returns number of bytes written to out (0 = failure).
// ---------------------------------------------------------------------------
inline size_t packet_encode_ack(const WheelState* s,
                                 const uint8_t     key[16],
                                 uint8_t*          out) {
    // Step 1: Build 9-byte SPP ACK
    uint8_t spp[9] = {
        SPP_ACK_PROTOCOL_ID,
        SPP_ACK_TELEGRAM_ID,
        SPP_ACK_SRC,
        SPP_ACK_DST,
        SPP_ACK_SERVICE,
        SPP_ACK_PARAM,
        (uint8_t)s->battery,
        s->assistLevel,
        s->driveProfile
    };

    // Step 2: PKCS7-pad to 16 bytes
    uint8_t padded[16];
    memcpy(padded, spp, sizeof(spp));
    const uint8_t padLen = (uint8_t)(16 - sizeof(spp));
    for (size_t i = sizeof(spp); i < 16; i++) padded[i] = padLen;

    // Step 3: Generate random IV
    uint8_t iv[16];
    crypto_generate_iv(iv);

    // Step 4: ECB-encrypt IV
    uint8_t ivEnc[16];
    if (!crypto_ecb_encrypt(key, iv, ivEnc)) return 0;

    // Step 5: CBC-encrypt padded SPP data
    uint8_t dataEnc[16];
    if (!crypto_cbc_encrypt(key, iv, padded, 16, dataEnc)) return 0;

    // Step 6: Assemble payload: [ivEnc(16)][dataEnc(16)]
    uint8_t payload[32];
    memcpy(payload,      ivEnc,   16);
    memcpy(payload + 16, dataEnc, 16);

    // Step 7: Build stuffed M25 frame
    return proto_frame_build(payload, 32, out);
}

#endif // PACKET_H
