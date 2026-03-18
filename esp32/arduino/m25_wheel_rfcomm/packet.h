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
#include "command.h"

// SPP response header constants
#define SPP_ACK_PROTOCOL_ID  0x23
#define SPP_ACK_TELEGRAM_ID  0x01
#define SPP_ACK_SRC          0x10   // Source: wheel
#define SPP_ACK_DST          0x01   // Destination: remote
#define SPP_ACK_SERVICE      0x01   // APP_MGMT (generic ACK)
#define SPP_ACK_PARAM        0xFF   // Generic ACK param

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
// _packet_encode_spp - shared: PKCS7-pad, encrypt, and frame spp bytes.
//   sppLen must be in [1, 16].
// ---------------------------------------------------------------------------
inline size_t _packet_encode_spp(const uint8_t* spp, size_t sppLen,
                                  const uint8_t  key[16], uint8_t* out) {
    uint8_t padded[16];
    memcpy(padded, spp, sppLen);
    const uint8_t padLen = (uint8_t)(16 - sppLen);
    for (size_t i = sppLen; i < 16; i++) padded[i] = padLen;

    uint8_t iv[16];
    crypto_generate_iv(iv);
    uint8_t ivEnc[16];
    if (!crypto_ecb_encrypt(key, iv, ivEnc)) return 0;
    uint8_t dataEnc[16];
    if (!crypto_cbc_encrypt(key, iv, padded, 16, dataEnc)) return 0;

    uint8_t payload[32];
    memcpy(payload,      ivEnc,   16);
    memcpy(payload + 16, dataEnc, 16);
    return proto_frame_build(payload, 32, out);
}

// ---------------------------------------------------------------------------
// packet_encode_ack - generic ACK (service=APP_MGMT, param=0xFF).
//   Used by CLI and as fallback; carries battery/assist/profile.
// ---------------------------------------------------------------------------
inline size_t packet_encode_ack(const WheelState* s,
                                 const uint8_t     key[16],
                                 uint8_t*          out) {
    uint8_t spp[9] = {
        SPP_ACK_PROTOCOL_ID, SPP_ACK_TELEGRAM_ID,
        SPP_ACK_SRC, SPP_ACK_DST,
        SPP_ACK_SERVICE, SPP_ACK_PARAM,
        (uint8_t)s->battery, s->assistLevel, s->driveProfile
    };
    return _packet_encode_spp(spp, sizeof(spp), key, out);
}

// ---------------------------------------------------------------------------
// packet_encode_response - context-aware response.
//   Mirrors reqService/reqParam back so the app can route the reply.
//   Falls back to packet_encode_ack() for unrecognised service IDs.
// ---------------------------------------------------------------------------
inline size_t packet_encode_response(uint8_t           reqService,
                                      uint8_t           reqParam,
                                      const WheelState* s,
                                      const uint8_t     key[16],
                                      uint8_t*          out) {
    uint8_t spp[9] = {
        SPP_ACK_PROTOCOL_ID, SPP_ACK_TELEGRAM_ID,
        SPP_ACK_SRC, SPP_ACK_DST,
        reqService, reqParam,
        0, 0, 0
    };
    size_t sppLen = 6;

    switch (reqService) {

        case SPP_SERVICE_BATT_MGMT:
            spp[6] = (uint8_t)s->battery;
            sppLen = 7;
            break;

        case SPP_SERVICE_VERSION_MGMT:
            spp[6] = FW_VERSION_MAJOR;
            spp[7] = FW_VERSION_MINOR;
            spp[8] = HW_VERSION;
            sppLen = 9;
            break;

        case SPP_SERVICE_APP_MGMT:
            switch (reqParam) {
                case SPP_PARAM_READ_SPEED:
                    spp[6] = (uint8_t)(s->speed >> 8);
                    spp[7] = (uint8_t)(s->speed & 0xFF);
                    sppLen = 8;
                    break;
                case SPP_PARAM_READ_DRIVE_MODE:
                    spp[6] = s->driveMode;
                    sppLen = 7;
                    break;
                case SPP_PARAM_READ_CRUISE:
                    spp[6] = (uint8_t)(s->cruiseSpeed >> 8);
                    spp[7] = (uint8_t)(s->cruiseSpeed & 0xFF);
                    sppLen = 8;
                    break;
                case SPP_PARAM_AUTO_SHUTOFF:
                    spp[6] = s->autoShutoffMin;
                    sppLen = 7;
                    break;
                default:
                    // Generic APP_MGMT reply carries wheel status
                    spp[4] = SPP_ACK_SERVICE;
                    spp[5] = SPP_ACK_PARAM;
                    spp[6] = (uint8_t)s->battery;
                    spp[7] = s->assistLevel;
                    spp[8] = s->driveProfile;
                    sppLen = 9;
            }
            break;

        default:
            return packet_encode_ack(s, key, out);
    }

    return _packet_encode_spp(spp, sppLen, key, out);
}

#endif // PACKET_H
