/*
 * m25_ble.h - BLE client for Alber e-motion M25 wheels
 *
 * Implements the complete M25 SPP-over-BLE communication stack:
 *   - AES-128-CBC encryption (IV encrypted via ECB, per m25_crypto.py)
 *   - CRC-16 frame checksum (per m25_protocol.py)
 *   - Byte stuffing of 0xEF markers (add_delimiters in m25_protocol.py)
 *   - SPP packet builder (per m25_spp.py PacketBuilder)
 *   - BLE GATT client connection to both wheels
 *
 * Protocol constants match m25_protocol_data.py exactly.
 *
 * Connection sequence (m25_parking.py):
 *   1. BLE GATT connect
 *   2. WRITE_SYSTEM_MODE(0x01)  -> init communication
 *   3. WRITE_DRIVE_MODE(0x04)   -> enable remote control bit
 *   4. WRITE_REMOTE_SPEED(spd)  -> every 50 ms while operating
 *   5. WRITE_REMOTE_SPEED(0) + WRITE_DRIVE_MODE(0x00) on disconnect
 *
 * Speed sign convention (m25_parking.py left_wheel_speed = -actual_left):
 *   LEFT  wheel: send NEGATED speed (wheels face opposite directions)
 *   RIGHT wheel: send AS-IS speed
 */

#ifndef M25_BLE_H
#define M25_BLE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <mbedtls/aes.h>
#include <esp_system.h>     // esp_fill_random()
#include <stddef.h>         // offsetof()
#include "device_config.h"

// External debug flags (defined in serial_commands.h)
extern uint8_t debugFlags;

// ---------------------------------------------------------------------------
// M25 SPP Service / Characteristic UUIDs (m25_bluetooth.py)
// ---------------------------------------------------------------------------
#define M25_SPP_SERVICE_UUID  "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_TX_UUID      "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_RX_UUID      "00001102-0000-1000-8000-00805F9B34FB"

// ---------------------------------------------------------------------------
// Protocol constants (m25_protocol.py / m25_protocol_data.py)
// ---------------------------------------------------------------------------
#define M25_HEADER_MARKER     0xEF
#define M25_HEADER_SIZE       3     // [0xEF, len_hi, len_lo]
#define M25_CRC_SIZE          2

// SPP header fixed field values
#define M25_PROTOCOL_ID_STANDARD    0x01
#define M25_SRC_SMARTPHONE          0x05
#define M25_DEST_WHEEL_COMMON       0x01

// Starting telegram ID (DEFAULT_TELEGRAM_ID in m25_protocol_data.py = -128 = 0x80)
#define M25_TELEGRAM_ID_START       0x80

// Service ID for all remote commands
#define M25_SRV_APP_MGMT            0x01

// Parameter IDs (APP_MGMT service, m25_protocol_data.py)
#define M25_PARAM_WRITE_SYSTEM_MODE   0x10
#define M25_PARAM_WRITE_DRIVE_MODE    0x20
#define M25_PARAM_WRITE_REMOTE_SPEED  0x30
#define M25_PARAM_WRITE_ASSIST_LEVEL  0x40

// Drive mode bit flags (DRIVE_MODE_BIT_* in m25_protocol_data.py)
#define M25_DRIVE_MODE_NORMAL     0x00
#define M25_DRIVE_MODE_AUTO_HOLD  0x01   // hill hold
#define M25_DRIVE_MODE_CRUISE     0x02
#define M25_DRIVE_MODE_REMOTE     0x04   // remote control

// System mode values
#define M25_SYSTEM_MODE_CONNECT   0x01
#define M25_SYSTEM_MODE_STANDBY   0x02

// Assist levels sent to wheel: ASSIST_LEVEL_1=0, _2=1, _3=2 (m25_protocol_data.py)
// Mapping: ASSIST_INDOOR -> 0, ASSIST_OUTDOOR -> 1, ASSIST_LEARNING -> 2
static const uint8_t M25_ASSIST_LEVEL_MAP[ASSIST_COUNT] = { 0, 1, 2 };

// Speed scaling: percent (-100..+100) to M25 raw signed int16 units.
// SPEED_FAST in m25_parking.py is ~250.  100 % -> 250 raw.
#define M25_SPEED_SCALE  2.5f

// Reconnect retry interval
#define BLE_RECONNECT_DELAY_MS    5000
// Stop auto-reconnect after this many consecutive failures per wheel
#define BLE_MAX_RECONNECT_FAILS   5

// ---------------------------------------------------------------------------
// Wheel slot indices
// ---------------------------------------------------------------------------
#define WHEEL_LEFT  0
#define WHEEL_RIGHT 1
#define WHEEL_COUNT 2

// ---------------------------------------------------------------------------
// CRC-16 lookup table (m25_protocol.py CRC_TABLE, init value 0xFFFF)
// ---------------------------------------------------------------------------
static const uint16_t _crcTable[256] PROGMEM = {
    0,49345,49537,320,49921,960,640,49729,50689,1728,1920,51009,1280,50625,50305,1088,
    52225,3264,3456,52545,3840,53185,52865,3648,2560,51905,52097,2880,51457,2496,2176,51265,
    55297,6336,6528,55617,6912,56257,55937,6720,7680,57025,57217,8000,56577,7616,7296,56385,
    5120,54465,54657,5440,55041,6080,5760,54849,53761,4800,4992,54081,4352,53697,53377,4160,
    61441,12480,12672,61761,13056,62401,62081,12864,13824,63169,63361,14144,62721,13760,13440,62529,
    15360,64705,64897,15680,65281,16320,16000,65089,64001,15040,15232,64321,14592,63937,63617,14400,
    10240,59585,59777,10560,60161,11200,10880,59969,60929,11968,12160,61249,11520,60865,60545,11328,
    58369,9408,9600,58689,9984,59329,59009,9792,8704,58049,58241,9024,57601,8640,8320,57409,
    40961,24768,24960,41281,25344,41921,41601,25152,26112,42689,42881,26432,42241,26048,25728,42049,
    27648,44225,44417,27968,44801,28608,28288,44609,43521,27328,27520,43841,26880,43457,43137,26688,
    30720,47297,47489,31040,47873,31680,31360,47681,48641,32448,32640,48961,32000,48577,48257,31808,
    46081,29888,30080,46401,30464,47041,46721,30272,29184,45761,45953,29504,45313,29120,28800,45121,
    20480,37057,37249,20800,37633,21440,21120,37441,38401,22208,22400,38721,21760,38337,38017,21568,
    39937,23744,23936,40257,24320,40897,40577,24128,23040,39617,39809,23360,39169,22976,22656,38977,
    34817,18624,18816,35137,19200,35777,35457,19008,19968,36545,36737,20288,36097,19904,19584,35905,
    17408,33985,34177,17728,34561,18368,18048,34369,33281,17088,17280,33601,16640,33217,32897,16448
};

// CRC-16 (m25_protocol.py calculate_crc)
static uint16_t _m25Crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ pgm_read_word(&_crcTable[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

// Byte stuffing (m25_protocol.py add_delimiters)
// First byte kept as-is; every subsequent 0xEF is doubled.
static size_t _addDelimiters(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];
    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        if (in[i] == M25_HEADER_MARKER) out[pos++] = M25_HEADER_MARKER;
    }
    return pos;
}

// ---------------------------------------------------------------------------
// Per-wheel connection state
// ---------------------------------------------------------------------------
struct WheelConnState_t {
    char                         mac[18];   // runtime-mutable "XX:XX:XX:XX:XX:XX\0"
    const char*                  name;
    uint8_t                      key[16];   // runtime-mutable AES-128 key
    bool                         connected;
    bool                         protocolReady;      // SYSTEM_MODE + DRIVE_MODE acked
    uint8_t                      telegramId;         // SPP sequence counter
    uint8_t                      driveModeBits;      // current DRIVE_MODE byte
    BLEClient*                   client;
    BLERemoteCharacteristic*     rxChar;             // For writing commands to wheel
    BLERemoteCharacteristic*     txChar;             // For receiving responses from wheel
    bool                         receivedFirstAck;   // Track if we got a response (encryption validated)
    uint32_t                     lastConnectAttemptMs;
    uint8_t                      consecutiveFails;   // resets on success; auto-reconnect stops at BLE_MAX_RECONNECT_FAILS
};

// ---------------------------------------------------------------------------
// Compile-time default keys (copied into mutable _wheels storage by bleInit)
// ---------------------------------------------------------------------------
static const uint8_t _keyDefaultLeft[16]  = ENCRYPTION_KEY_LEFT;
static const uint8_t _keyDefaultRight[16] = ENCRYPTION_KEY_RIGHT;

// ---------------------------------------------------------------------------
// Global wheel state (zero-initialised; fully populated in bleInit)
// Auto-reconnect flag: when false, bleTick() skips reconnect attempts.
// Using function-static to ensure single shared instance across translation units
// ---------------------------------------------------------------------------
inline WheelConnState_t* _getWheels() {
    static WheelConnState_t wheels[WHEEL_COUNT];
    return wheels;
}
#define _wheels (_getWheels())

inline bool& _getBleAutoReconnect() {
    static bool autoReconnect = true;
    return autoReconnect;
}
#define _bleAutoReconnect (_getBleAutoReconnect())

// ---------------------------------------------------------------------------
// Wheel activity filter (compile-time, driven by WHEEL_MODE in device_config.h)
// Every function that iterates _wheels[] calls this guard so inactive wheels
// are silently skipped for connect, reconnect, and command dispatch.
// ---------------------------------------------------------------------------
static inline bool _wheelActive(int idx) {
#if   WHEEL_MODE == WHEEL_MODE_DUAL
    return true;
#elif WHEEL_MODE == WHEEL_MODE_LEFT_ONLY
    return idx == WHEEL_LEFT;
#elif WHEEL_MODE == WHEEL_MODE_RIGHT_ONLY
    return idx == WHEEL_RIGHT;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Encrypt an SPP plaintext packet into a complete M25 BLE frame.
// (m25_crypto.py M25Encryptor.encrypt + m25_protocol.py build_packet)
//
//   spp     : plaintext SPP packet (6..N bytes)
//   sppLen  : byte count
//   key     : 16-byte AES key
//   out     : output buffer >= 128 bytes
//   outLen  : actual bytes written
//
// Frame structure (post byte-stuffing):
//   [0xEF] [len_hi] [len_lo] [iv_ecb(16)] [aes_cbc(padded_spp)] [crc_hi] [crc_lo]
//   (+ doubled 0xEF bytes inside from _addDelimiters)
// ---------------------------------------------------------------------------
static bool _m25Encrypt(const uint8_t* key, const uint8_t* spp, uint8_t sppLen,
                         uint8_t* out, size_t* outLen) {
    if (!key || !spp || !out || !outLen) {
        Serial.println("[BLE-ENC] ERROR: NULL parameter provided");
        return false;
    }
    
    // 1. PKCS7 pad SPP to 16-byte boundary
    uint8_t padLen    = (uint8_t)(16 - (sppLen % 16));
    uint8_t paddedLen = sppLen + padLen;   // always 16 or 32 for our packets

    uint8_t padded[32];
    memcpy(padded, spp, sppLen);
    memset(padded + sppLen, padLen, padLen);

    // 2. Random IV
    uint8_t iv[16];
    esp_fill_random(iv, sizeof(iv));
    
    // Verify RNG is working (all zeros would indicate uninitialized RNG)
    bool allZeros = true;
    for (int i = 0; i < 16; i++) {
        if (iv[i] != 0) {
            allZeros = false;
            break;
        }
    }
    if (allZeros) {
        Serial.println("[BLE-ENC] WARNING: RNG returned all zeros, retrying...");
        delay(50);
        esp_fill_random(iv, sizeof(iv));
    }

    // 3. Encrypt IV with AES-128-ECB  (iv_encrypted = AES_ECB(key).encrypt(iv))
    uint8_t ivEnc[16];
    {
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key, 128);
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, iv, ivEnc);
        mbedtls_aes_free(&ctx);
    }

    // 4. Encrypt padded SPP with AES-128-CBC  (AES_CBC(key, iv).encrypt(padded))
    uint8_t encData[32];
    {
        uint8_t ivCopy[16];
        memcpy(ivCopy, iv, 16);   // mbedtls_aes_crypt_cbc modifies IV in place
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key, 128);
        mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, paddedLen,
                               ivCopy, padded, encData);
        mbedtls_aes_free(&ctx);
    }

    // 5. Build payload = iv_encrypted(16) + encrypted_data(paddedLen)
    uint8_t payload[64];
    memcpy(payload,      ivEnc,   16);
    memcpy(payload + 16, encData, paddedLen);
    uint8_t payloadLen = (uint8_t)(16 + paddedLen);

    // 6. Build raw frame: [0xEF][len_hi][len_lo][payload]
    //    frame_length = HEADER_SIZE + payloadLen + CRC_SIZE - 1
    uint16_t frameLength = (uint16_t)(M25_HEADER_SIZE + payloadLen + M25_CRC_SIZE - 1);

    uint8_t frame[128];
    frame[0] = M25_HEADER_MARKER;
    frame[1] = (uint8_t)((frameLength >> 8) & 0xFF);
    frame[2] = (uint8_t)(frameLength & 0xFF);
    memcpy(frame + 3, payload, payloadLen);
    size_t frameLen = (size_t)(3 + payloadLen);

    // 7. Append CRC-16 (over the header + payload bytes, before byte stuffing)
    uint16_t crc = _m25Crc16(frame, frameLen);
    frame[frameLen]     = (uint8_t)((crc >> 8) & 0xFF);
    frame[frameLen + 1] = (uint8_t)(crc & 0xFF);
    frameLen += 2;

    // 8. Byte stuffing -> final wire packet
    *outLen = _addDelimiters(frame, frameLen, out);
    return true;
}

// ---------------------------------------------------------------------------
// Build SPP packet, encrypt, and write to output buffer.
// Returns byte count, or 0 on error.   out must be >= 128 bytes.
// ---------------------------------------------------------------------------
static size_t _buildAndEncrypt(int idx, uint8_t serviceId, uint8_t paramId,
                                const uint8_t* payload, uint8_t payloadLen,
                                uint8_t* out) {
    WheelConnState_t &w = _wheels[idx];

    // Assemble plaintext SPP packet (6-byte header + payload)
    uint8_t spp[32];
    uint8_t n = 0;
    spp[n++] = M25_PROTOCOL_ID_STANDARD;
    spp[n++] = w.telegramId;
    w.telegramId = (uint8_t)((w.telegramId + 1) & 0xFF);
    spp[n++] = M25_SRC_SMARTPHONE;
    spp[n++] = M25_DEST_WHEEL_COMMON;
    spp[n++] = serviceId;
    spp[n++] = paramId;
    if (payload && payloadLen > 0) {
        memcpy(spp + n, payload, payloadLen);
        n += payloadLen;
    }

    size_t encLen = 0;
    if (!_m25Encrypt(w.key, spp, n, out, &encLen)) return 0;
    return encLen;
}

static bool _sendCommand(int idx, uint8_t serviceId, uint8_t paramId,
                          const uint8_t* payload = nullptr, uint8_t payloadLen = 0) {
    WheelConnState_t &w = _wheels[idx];
    // Double-guard: w.connected is set by the BLE callback thread (may lag);
    // client->isConnected() is the synchronous ground truth and prevents
    // writeValue() from blocking on a dead GATT connection.
    if (!w.connected || w.rxChar == nullptr) return false;
    if (w.client == nullptr || !w.client->isConnected()) {
        w.connected     = false;   // sync flag with reality
        w.protocolReady = false;
        return false;
    }
    uint8_t buf[128];
    size_t len = _buildAndEncrypt(idx, serviceId, paramId, payload, payloadLen, buf);
    if (len == 0) return false;
    w.rxChar->writeValue(buf, len, false);
    return true;
}

// ---------------------------------------------------------------------------
// Remove byte stuffing from received packet
// ---------------------------------------------------------------------------
static size_t _removeDelimiters(const uint8_t* in, size_t inLen, uint8_t* out, size_t outMax) {
    if (inLen == 0) return 0;
    
    size_t pos = 0;
    bool lastWasEF = false;
    
    for (size_t i = 0; i < inLen && pos < outMax; i++) {
        if (in[i] == M25_HEADER_MARKER) {
            if (lastWasEF) {
                // This is the second 0xEF in a row, skip it
                lastWasEF = false;
            } else {
                // First 0xEF
                out[pos++] = in[i];
                lastWasEF = true;
            }
        } else {
            out[pos++] = in[i];
            lastWasEF = false;
        }
    }
    return pos;
}

// ---------------------------------------------------------------------------
// Decrypt M25 BLE frame into plaintext SPP packet
// Frame structure: [0xEF][len_hi][len_lo][iv_ecb(16)][aes_cbc(padded_spp)][crc_hi][crc_lo]
// ---------------------------------------------------------------------------
static bool _m25Decrypt(const uint8_t* key, const uint8_t* frame, size_t frameLen,
                         uint8_t* sppOut, size_t* sppLen) {
    if (!key || !frame || !sppOut || !sppLen) {
        Serial.println("[BLE-DEC] ERROR: NULL parameter provided");
        return false;
    }
    
    // Verify frame has minimum size
    if (frameLen < M25_HEADER_SIZE + 16 + 16 + M25_CRC_SIZE) {
        return false;
    }
    
    // Parse header
    if (frame[0] != M25_HEADER_MARKER) return false;
    uint16_t declaredLen = ((uint16_t)frame[1] << 8) | frame[2];
    
    // Verify CRC (over everything before the CRC bytes)
    size_t crcPos = frameLen - M25_CRC_SIZE;
    uint16_t expectedCrc = _m25Crc16(frame, crcPos);
    uint16_t receivedCrc = ((uint16_t)frame[crcPos] << 8) | frame[crcPos + 1];
    if (expectedCrc != receivedCrc) {
        Serial.printf("[BLE-DEC] CRC mismatch: expected 0x%04X, got 0x%04X\n", 
                     expectedCrc, receivedCrc);
        return false;
    }
    
    // Extract encrypted IV (16 bytes after header)
    const uint8_t* ivEnc = frame + M25_HEADER_SIZE;
    
    // Calculate encrypted data length (payload - IV - CRC)
    size_t encDataLen = frameLen - M25_HEADER_SIZE - 16 - M25_CRC_SIZE;
    if (encDataLen == 0 || encDataLen % 16 != 0) return false;
    const uint8_t* encData = ivEnc + 16;
    
    // Decrypt IV with AES-128-ECB
    uint8_t iv[16];
    {
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_dec(&ctx, key, 128);
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, ivEnc, iv);
        mbedtls_aes_free(&ctx);
    }
    
    // Decrypt data with AES-128-CBC
    uint8_t decrypted[64];
    {
        uint8_t ivCopy[16];
        memcpy(ivCopy, iv, 16);
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_dec(&ctx, key, 128);
        mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, encDataLen,
                               ivCopy, encData, decrypted);
        mbedtls_aes_free(&ctx);
    }
    
    // Remove PKCS7 padding
    uint8_t padLen = decrypted[encDataLen - 1];
    if (padLen == 0 || padLen > 16 || padLen > encDataLen) {
        Serial.printf("[BLE-DEC] Invalid PKCS7 padding: %d\n", padLen);
        return false;
    }
    
    // Verify padding bytes
    for (size_t i = encDataLen - padLen; i < encDataLen; i++) {
        if (decrypted[i] != padLen) {
            Serial.printf("[BLE-DEC] PKCS7 padding verification failed\n");
            return false;
        }
    }
    
    *sppLen = encDataLen - padLen;
    memcpy(sppOut, decrypted, *sppLen);
    return true;
}

// ---------------------------------------------------------------------------
// Parse SPP packet structure
// SPP format: [protocol_id][telegram_id][src_id][dest_id][service_id][param_id][payload...]
// ---------------------------------------------------------------------------
static void _parseSppPacket(const uint8_t* spp, size_t sppLen, const char* wheelName) {
    if (sppLen < 6) {
        Serial.printf("[BLE] %s wheel: SPP too short (%zu bytes)\n", wheelName, sppLen);
        return;
    }
    
    uint8_t protocolId = spp[0];
    uint8_t telegramId = spp[1];
    uint8_t srcId      = spp[2];
    uint8_t destId     = spp[3];
    uint8_t serviceId  = spp[4];
    uint8_t paramId    = spp[5];
    size_t payloadLen  = sppLen - 6;
    
    Serial.printf("[BLE] %s wheel response:\n", wheelName);
    Serial.printf("  Protocol: 0x%02X, Telegram: 0x%02X\n", protocolId, telegramId);
    Serial.printf("  Src: 0x%02X, Dest: 0x%02X\n", srcId, destId);
    Serial.printf("  Service: 0x%02X, Param: 0x%02X\n", serviceId, paramId);
    
    if (payloadLen > 0) {
        Serial.printf("  Payload (%zu bytes): ", payloadLen);
        for (size_t i = 0; i < payloadLen && i < 16; i++) {
            Serial.printf("%02X ", spp[6 + i]);
        }
        if (payloadLen > 16) Serial.print("...");
        Serial.println();
    }
    
    // Parse specific response types
    if (serviceId == M25_SRV_APP_MGMT) {
        if (paramId == M25_PARAM_WRITE_SYSTEM_MODE && payloadLen >= 1) {
            Serial.printf("    System Mode ACK: 0x%02X\n", spp[6]);
        } else if (paramId == M25_PARAM_WRITE_DRIVE_MODE && payloadLen >= 1) {
            uint8_t mode = spp[6];
            Serial.printf("    Drive Mode ACK: 0x%02X (", mode);
            if (mode & M25_DRIVE_MODE_REMOTE) Serial.print("REMOTE ");
            if (mode & M25_DRIVE_MODE_CRUISE) Serial.print("CRUISE ");
            if (mode & M25_DRIVE_MODE_AUTO_HOLD) Serial.print("AUTO_HOLD ");
            if (mode == 0) Serial.print("NORMAL");
            Serial.println(")");
        } else if (paramId == M25_PARAM_WRITE_REMOTE_SPEED && payloadLen >= 2) {
            int16_t speed = ((int16_t)spp[6] << 8) | spp[7];
            Serial.printf("    Speed ACK: %d raw\n", speed);
        }
    }
}

// ---------------------------------------------------------------------------
// BLE notification callback - receives responses from wheels
// ---------------------------------------------------------------------------
static void _notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    // Find which wheel this notification is for
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheels[i].txChar == pChar) {
            WheelConnState_t &w = _wheels[i];
            
            // Always show raw data for debugging decryption issues
            if (debugFlags & DBG_BLE) {
                Serial.print("[BLE] Raw data: ");
                for (size_t i = 0; i < length && i < 48; i++) {
                    Serial.printf("%02X ", pData[i]);
                }
                if (length > 48) Serial.print("...");
                Serial.println();
            }
            
            // Remove byte stuffing
            uint8_t unstuffed[128];
            size_t unstuffedLen = _removeDelimiters(pData, length, unstuffed, sizeof(unstuffed));
            
            if (debugFlags & DBG_BLE) {
                Serial.printf("[BLE] After unstuffing: %zu bytes\n", unstuffedLen);
                if (unstuffedLen != length) {
                    Serial.print("  Unstuffed: ");
                    for (size_t i = 0; i < unstuffedLen && i < 48; i++) {
                        Serial.printf("%02X ", unstuffed[i]);
                    }
                    if (unstuffedLen > 48) Serial.print("...");
                    Serial.println();
                }
            }
            
            // Check minimum frame size before attempting decrypt
            if (unstuffedLen < M25_HEADER_SIZE + 16 + 16 + M25_CRC_SIZE) {
                Serial.printf("[BLE] %s wheel: Frame too short (%zu bytes, need >= 37)\n", 
                             w.name ? w.name : "Unknown", unstuffedLen);
                break;
            }
            
            // Decrypt frame
            uint8_t sppPacket[64];
            size_t sppLen = 0;
            if (_m25Decrypt(w.key, unstuffed, unstuffedLen, sppPacket, &sppLen)) {
                // Mark first successful decryption (encryption validated)
                if (!w.receivedFirstAck) {
                    w.receivedFirstAck = true;
                    Serial.printf("[BLE] %s wheel: First response received (%zu bytes) - encryption validated\n", 
                                 w.name ? w.name : "Unknown", length);
                }
                
                // Parse SPP packet structure
                _parseSppPacket(sppPacket, sppLen, w.name ? w.name : "Unknown");
            } else {
                Serial.printf("[BLE] %s wheel: Decryption failed\n", w.name ? w.name : "Unknown");
            }
            
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// BLE disconnect callback
// ---------------------------------------------------------------------------
class M25DisconnectCallback : public BLEClientCallbacks {
public:
    uint8_t wheelIdx;
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override {
        _wheels[wheelIdx].connected     = false;
        _wheels[wheelIdx].protocolReady = false;
        _wheels[wheelIdx].driveModeBits = 0;
        
        // Only print message for active wheels (respect WHEEL_MODE)
        if (_wheelActive(wheelIdx)) {
            Serial.printf("[BLE] %s wheel disconnected\n", _wheels[wheelIdx].name);
        }
    }
};
static M25DisconnectCallback _callbacks[WHEEL_COUNT];

// ---------------------------------------------------------------------------
// Initialization - call once in setup()
// ---------------------------------------------------------------------------
inline void bleInit(const char* deviceName = "M25-Remote") {
    // Populate mutable wheel config from compile-time device_config.h defaults
    memset(_wheels, 0, sizeof(_wheels));
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] Wheels array after memset: %p (size: %d bytes)\n", 
                      (void*)_wheels, sizeof(_wheels));
    }
    
    strncpy(_wheels[WHEEL_LEFT].mac,  LEFT_WHEEL_MAC,  17);
    strncpy(_wheels[WHEEL_RIGHT].mac, RIGHT_WHEEL_MAC, 17);
    _wheels[WHEEL_LEFT].name   = "Left";
    _wheels[WHEEL_RIGHT].name  = "Right";
    memcpy(_wheels[WHEEL_LEFT].key,  _keyDefaultLeft,  16);
    memcpy(_wheels[WHEEL_RIGHT].key, _keyDefaultRight, 16);
    _wheels[WHEEL_LEFT].telegramId  = M25_TELEGRAM_ID_START;
    _wheels[WHEEL_RIGHT].telegramId = M25_TELEGRAM_ID_START;
    
    // Verify initialization
    if (debugFlags & DBG_BLE) {
        for (int i = 0; i < WHEEL_COUNT; i++) {
            Serial.printf("[BLE] _wheels[%d]: name=%s, mac=%s, client=%p\n",
                          i, _wheels[i].name ? _wheels[i].name : "NULL",
                          _wheels[i].mac, (void*)_wheels[i].client);
        }
    }

    BLEDevice::init(deviceName);
    Serial.println("[BLE] Device initialized");
    Serial.printf("[BLE] Wheel mode: %s\n", WHEEL_MODE_NAME);
    
    // Ensure hardware RNG is seeded after BLE init
    // (esp_fill_random needs BLE radio active for entropy)
    delay(100);
    
    // Test RNG is working by generating test values
    uint32_t testRandom[4];
    esp_fill_random(testRandom, sizeof(testRandom));
    bool rngWorking = false;
    for (int i = 0; i < 4; i++) {
        if (testRandom[i] != 0 && testRandom[i] != 0xFFFFFFFF) {
            rngWorking = true;
            break;
        }
    }
    if (!rngWorking) {
        Serial.println("[BLE] WARNING: RNG may not be seeded, waiting longer...");
        delay(500);
    } else {
        Serial.println("[BLE] RNG verified");
    }

    for (int i = 0; i < WHEEL_COUNT; i++) {
        _callbacks[i].wheelIdx = (uint8_t)i;
    }
}

// ---------------------------------------------------------------------------
// Connect to a single wheel and run the M25 protocol init sequence.
// Blocking (uses delay()) - only called during STATE_CONNECTING.
// ---------------------------------------------------------------------------
static bool _connectWheel(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) {
        Serial.printf("[BLE] ERROR: Invalid wheel index %d\n", idx);
        return false;
    }
    
    WheelConnState_t &w = _wheels[idx];
    
    // CRITICAL: Check if name pointer is corrupted
    if (!w.name) {
        Serial.printf("[BLE] CRITICAL ERROR: _wheels[%d].name is NULL!\n", idx);
        Serial.printf("[BLE] Struct address: %p, name offset: %lu\n", 
                      (void*)&w, offsetof(WheelConnState_t, name));
        Serial.printf("[BLE] Attempting to restore name pointer...\n");
        // Restore the name from the expected value
        if (idx == WHEEL_LEFT) {
            w.name = "Left";
        } else if (idx == WHEEL_RIGHT) {
            w.name = "Right";
        }
    }
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] _connectWheel(%d): %s wheel\n", idx, w.name ? w.name : "UNKNOWN");
        Serial.printf("[BLE] MAC: '%s' (length: %d)\n", w.mac, strlen(w.mac));
    }
    
    w.telegramId    = M25_TELEGRAM_ID_START;
    w.driveModeBits = 0;
    w.protocolReady = false;

    const char* wheelName = w.name ? w.name : "Unknown";
    Serial.printf("[BLE] Connecting to %s wheel (%s)...\n", wheelName, w.mac);

    if (w.client == nullptr) {
        w.client = BLEDevice::createClient();
        if (w.client == nullptr) {
            Serial.printf("[BLE] %s wheel: Failed to create BLE client\n", wheelName);
            w.consecutiveFails++;
            return false;
        }
        Serial.println("[BLE] Setting client callbacks...");
        w.client->setClientCallbacks(&_callbacks[idx]);
    }

    // Validate MAC address format before using it
    if (strlen(w.mac) != 17) {
        Serial.printf("[BLE] ERROR: Invalid MAC address length: %d (expected 17)\n", strlen(w.mac));
        w.consecutiveFails++;
        return false;
    }

    Serial.printf("[BLE] Connecting to BLE address %s...\n", w.mac);
    if (!w.client->connect(BLEAddress(w.mac))) {
        Serial.printf("[BLE] %s wheel: GATT connect FAILED\n", wheelName);
        w.consecutiveFails++;
        return false;
    }

    BLERemoteService* svc = w.client->getService(BLEUUID(M25_SPP_SERVICE_UUID));
    if (!svc) {
        Serial.printf("[BLE] %s wheel: SPP service not found\n", wheelName);
        w.client->disconnect();
        w.consecutiveFails++;
        return false;
    }

    w.rxChar = svc->getCharacteristic(BLEUUID(M25_CHAR_RX_UUID));
    if (!w.rxChar) {
        Serial.printf("[BLE] %s wheel: RX characteristic not found\n", wheelName);
        w.client->disconnect();
        w.consecutiveFails++;
        return false;
    }
    
    // Get TX characteristic for receiving notifications (optional - wheel may not send)
    w.txChar = svc->getCharacteristic(BLEUUID(M25_CHAR_TX_UUID));
    if (w.txChar && w.txChar->canNotify()) {
        w.txChar->registerForNotify(_notifyCallback);
        Serial.printf("[BLE] %s wheel: Notifications enabled\n", wheelName);
    }
    w.receivedFirstAck = false;  // Reset ACK flag

    w.connected = true;

    // Small delay to ensure BLE GATT is fully established
    delay(50);

    // M25 protocol init sequence (m25_parking.py connect())
    // Step 0: WRITE_SYSTEM_MODE = 0x01 (initialize communication)
    uint8_t sysMode = M25_SYSTEM_MODE_CONNECT;
    _sendCommand(idx, M25_SRV_APP_MGMT, M25_PARAM_WRITE_SYSTEM_MODE, &sysMode, 1);
    
    // Wait for first ACK response to validate encryption (non-blocking loop)
    uint32_t waitStart = millis();
    while (!w.receivedFirstAck && (millis() - waitStart < 2000)) {
        extern void ledTick();
        extern void buzzerTick();
        ledTick();
        buzzerTick();
        delay(10);
    }
    
    if (!w.receivedFirstAck) {
        Serial.printf("[BLE] %s wheel: No response to SYSTEM_MODE (encryption validation failed)\n", wheelName);
        w.client->disconnect();
        w.consecutiveFails++;
        w.connected = false;
        return false;
    }

    // Step 1: WRITE_DRIVE_MODE = 0x04 (enable remote bit)
    uint8_t driveMode = M25_DRIVE_MODE_REMOTE;
    _sendCommand(idx, M25_SRV_APP_MGMT, M25_PARAM_WRITE_DRIVE_MODE, &driveMode, 1);
    w.driveModeBits = M25_DRIVE_MODE_REMOTE;
    
    // Brief delay for drive mode to take effect
    waitStart = millis();
    while (millis() - waitStart < 200) {
        extern void ledTick();
        extern void buzzerTick();
        ledTick();
        buzzerTick();
        delay(1);
    }

    w.protocolReady = true;
    w.consecutiveFails = 0;
    Serial.printf("[BLE] %s wheel ready\n", wheelName);
    return true;
}

// ---------------------------------------------------------------------------
// Connect to active wheels (call after bleInit)
// Inactive wheels (per WHEEL_MODE) are silently skipped.
// ---------------------------------------------------------------------------
inline void bleConnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && !_wheels[i].connected) {
            if (debugFlags & DBG_BLE) {
                Serial.printf("[BLE] bleConnect: About to connect wheel %d, MAC='%s' (len=%d)\n",
                              i, _wheels[i].mac, strlen(_wheels[i].mac));
            }
            _connectWheel(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Disconnect all wheels - stop motors and disable remote mode first
// (m25_parking.py disconnect sequence)
// ---------------------------------------------------------------------------
inline void bleDisconnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        WheelConnState_t &w = _wheels[i];
        if (w.connected && w.rxChar) {
            // Step 3: WRITE_REMOTE_SPEED = 0  (stop)
            uint8_t spd[2] = { 0, 0 };
            _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
            
            // Wait briefly for command to send (non-blocking loop)
            uint32_t waitStart = millis();
            while (millis() - waitStart < 50) {
                extern void ledTick();
                extern void buzzerTick();
                ledTick();
                buzzerTick();
                delay(1);
            }
            
            // Step 4: WRITE_DRIVE_MODE = 0x00  (normal / release remote bit)
            uint8_t norm = M25_DRIVE_MODE_NORMAL;
            _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_DRIVE_MODE, &norm, 1);
        }
        if (w.client && w.client->isConnected()) w.client->disconnect();
        w.connected     = false;
        w.protocolReady = false;
        w.driveModeBits = 0;
    }
}

// ---------------------------------------------------------------------------
// Connection status queries
// ---------------------------------------------------------------------------
inline bool bleIsConnected(int wheelIdx) {
    WheelConnState_t &w = _wheels[wheelIdx];
    return w.connected && w.protocolReady
        && w.client != nullptr && w.client->isConnected();
}

// True when every *active* wheel (per WHEEL_MODE) is connected and protocol-ready.
inline bool bleAllConnected() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && !bleIsConnected(i)) return false;
    }
    return true;
}

// True when at least one active wheel is connected.
inline bool bleAnyConnected() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && bleIsConnected(i)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public API: Stop both motors (WRITE_REMOTE_SPEED = 0)
// ---------------------------------------------------------------------------
inline bool bleSendStop() {
    uint8_t spd[2] = { 0, 0 };
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Public API: Motor speed command
//
//  leftPercent  : -100..+100  (positive = forward)
//  rightPercent : -100..+100  (positive = forward)
//
// Sign convention (m25_parking.py):
//   Left  wheel: negate speed (wheels face opposite directions)
//   Right wheel: keep sign
//
// Encoding: big-endian signed int16 (struct.pack('>h', speed) in Python)
// Scale:    100 % -> 250 raw units (SPEED_FAST in m25_parking.py)
// ---------------------------------------------------------------------------
inline bool bleSendMotorCommand(float leftPercent, float rightPercent) {
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!bleIsConnected(i)) continue;

        float pct = (i == WHEEL_LEFT) ? -leftPercent : rightPercent;
        int16_t raw = (int16_t)constrain(pct * M25_SPEED_SCALE, -32768.0f, 32767.0f);

        // Big-endian int16
        uint8_t spd[2] = {
            (uint8_t)((raw >> 8) & 0xFF),
            (uint8_t)(raw & 0xFF)
        };
        ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Public API: Hill hold (WRITE_DRIVE_MODE with AUTO_HOLD bit)
//
// Preserves the REMOTE bit already stored in driveModeBits.
// ---------------------------------------------------------------------------
inline bool bleSendHillHold(bool enable) {
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!bleIsConnected(i)) continue;
        WheelConnState_t &w = _wheels[i];
        if (enable) {
            w.driveModeBits |=  M25_DRIVE_MODE_AUTO_HOLD;
        } else {
            w.driveModeBits &= (uint8_t)~M25_DRIVE_MODE_AUTO_HOLD;
        }
        uint8_t dm = w.driveModeBits;
        ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_DRIVE_MODE, &dm, 1);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Public API: Assist level (WRITE_ASSIST_LEVEL)
//
//  level: ASSIST_INDOOR(0), ASSIST_OUTDOOR(1), ASSIST_LEARNING(2)
//  Mapped to M25 level bytes 0, 1, 2 via M25_ASSIST_LEVEL_MAP[].
// ---------------------------------------------------------------------------
inline bool bleSendAssistLevel(uint8_t level) {
    if (level >= ASSIST_COUNT) level = 0;
    uint8_t m25Level = M25_ASSIST_LEVEL_MAP[level];
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_ASSIST_LEVEL,
                               &m25Level, 1);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Background tick: attempt reconnect if a wheel dropped out.
// Call from loop().
// ---------------------------------------------------------------------------
inline void bleTick() {
    if (!_bleAutoReconnect) return;
    uint32_t now = millis();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        WheelConnState_t &w = _wheels[i];
        if (!w.connected) {
            if (now - w.lastConnectAttemptMs >= BLE_RECONNECT_DELAY_MS) {
                w.lastConnectAttemptMs = now;
                if (debugFlags & DBG_BLE) {  // DBG_BLE
                    Serial.printf("[BLE] Attempting reconnect to %s wheel... (attempt %u/%u)\n",
                                  w.name, (unsigned)(w.consecutiveFails + 1),
                                  (unsigned)BLE_MAX_RECONNECT_FAILS);
                }
                _connectWheel(i);
                if (w.consecutiveFails >= BLE_MAX_RECONNECT_FAILS) {
                    Serial.printf("[BLE] %s wheel: %u consecutive failures - "
                                  "disabling auto-reconnect. Use 'autoreconnect on' to retry.\n",
                                  w.name, (unsigned)w.consecutiveFails);
                    _bleAutoReconnect = false;
                    return;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-reconnect control
// ---------------------------------------------------------------------------
inline void bleSetAutoReconnect(bool enable) {
    _bleAutoReconnect = enable;
    if (enable) {
        // Reset failure counters so the fresh run gets a clean slate
        for (int i = 0; i < WHEEL_COUNT; i++) {
            _wheels[i].consecutiveFails = 0;
        }
    }
    Serial.printf("[BLE] Auto-reconnect: %s\n", enable ? "ON" : "off");
}

inline bool bleGetAutoReconnect() {
    return _bleAutoReconnect;
}

// ---------------------------------------------------------------------------
// Runtime MAC address override
// Disconnects the affected wheel immediately; manual reconnect required.
// mac must be a 17-char "XX:XX:XX:XX:XX:XX" string.
// ---------------------------------------------------------------------------
inline void bleSetMac(int idx, const char* mac) {
    if (idx < 0 || idx >= WHEEL_COUNT) return;
    if (!_wheelActive(idx)) {
        if (debugFlags & DBG_BLE) {
            Serial.printf("[BLE] bleSetMac: Skipping inactive wheel %d\n", idx);
        }
        return;
    }
    if (!mac) {
        Serial.println("[BLE] ERROR: NULL MAC address provided");
        return;
    }
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] bleSetMac(%d, %s)\n", idx, mac);
    }
    
    // Validate array access
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] Accessing _wheels[%d]...\n", idx);
    }
    WheelConnState_t &w = _wheels[idx];
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] Got reference to wheel struct (name=%s)\n", w.name ? w.name : "NULL");
    }
    
    // Safely check and disconnect existing client
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] Checking client pointer: %p\n", (void*)w.client);
    }
    if (w.client != nullptr) {
        if (debugFlags & DBG_BLE) {
            Serial.println("[BLE] Client exists, checking connection...");
        }
        if (w.client->isConnected()) {
            if (debugFlags & DBG_BLE) {
                Serial.println("[BLE] Disconnecting...");
            }
            w.client->disconnect();
        }
    }
    if (debugFlags & DBG_BLE) {
        Serial.println("[BLE] Updating wheel state...");
    }
    w.connected     = false;
    w.protocolReady = false;
    w.consecutiveFails = 0;
    strncpy(w.mac, mac, 17);
    w.mac[17] = '\0';
    Serial.printf("[BLE] %s wheel MAC -> %s  (reconnect required)\n", 
                  w.name ? w.name : "Unknown", w.mac);
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] bleSetMac returning: _wheels[%d].mac='%s' (len=%d)\n",
                      idx, w.mac, strlen(w.mac));
    }
}

// ---------------------------------------------------------------------------
// Runtime AES-128 key override (16 raw bytes)
// The wheel must be reconnected after a key change.
// ---------------------------------------------------------------------------
inline void bleSetKey(int idx, const uint8_t* newKey) {
    if (idx < 0 || idx >= WHEEL_COUNT) return;
    if (!_wheelActive(idx)) {
        if (debugFlags & DBG_BLE) {
            Serial.printf("[BLE] bleSetKey: Skipping inactive wheel %d\n", idx);
        }
        return;
    }
    if (!newKey) {
        Serial.println("[BLE] ERROR: NULL key provided");
        return;
    }
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] bleSetKey(%d, [key data])\n", idx);
    }
    
    // Validate destination before memcpy
    WheelConnState_t &w = _wheels[idx];
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] _wheels[%d].key address: %p\n", idx, (void*)w.key);
        Serial.printf("[BLE] _wheels[%d].mac address: %p\n", idx, (void*)w.mac);
        Serial.printf("[BLE] _wheels[%d].name before: %s\n", idx, w.name ? w.name : "NULL");
        Serial.printf("[BLE] _wheels[%d].mac before: '%s' (len=%d)\n", idx, w.mac, strlen(w.mac));
    }
    
    memcpy(w.key, newKey, 16);
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] _wheels[%d].name after: %s\n", idx, w.name ? w.name : "NULL");
        Serial.printf("[BLE] _wheels[%d].mac after: '%s' (len=%d)\n", idx, w.mac, strlen(w.mac));
    }
    
    if (w.name) {
        Serial.printf("[BLE] %s wheel key updated  (reconnect required)\n", w.name);
    } else {
        Serial.printf("[BLE] Wheel %d key updated  (reconnect required)\n", idx);
    }
}

// ---------------------------------------------------------------------------
// Verbose per-wheel status dump (called by serial 'wheels' command)
// ---------------------------------------------------------------------------
inline void blePrintWheelDetails() {
    Serial.printf("[BLE] Wheel mode    : %s\n", WHEEL_MODE_NAME);
    for (int i = 0; i < WHEEL_COUNT; i++) {
        WheelConnState_t &w = _wheels[i];
        if (!_wheelActive(i)) {
            Serial.printf("[Wheel %s] INACTIVE (WHEEL_MODE = %s)\n",
                          w.name, WHEEL_MODE_NAME);
            continue;
        }
        Serial.printf("[Wheel %s]\n", w.name);
        Serial.printf("  MAC          : %s\n", w.mac);
        Serial.printf("  Key (hex)    : ");
        for (int b = 0; b < 16; b++) {
            Serial.printf("%02X", w.key[b]);
            if (b < 15) Serial.print(' ');
        }
        Serial.println();
        Serial.printf("  connected    : %s\n", w.connected     ? "yes" : "no");
        Serial.printf("  protocolRdy  : %s\n", w.protocolReady ? "yes" : "no");
        Serial.printf("  failCount    : %u / %u\n", (unsigned)w.consecutiveFails,
                                                     (unsigned)BLE_MAX_RECONNECT_FAILS);
        Serial.printf("  driveMode    : 0x%02X\n", w.driveModeBits);
        Serial.printf("  telegramId   : %u\n",     (unsigned)w.telegramId);
    }
    Serial.printf("[BLE] autoReconnect: %s\n", _bleAutoReconnect ? "ON" : "off");
}

#endif // M25_BLE_H
