/*
 * m25_ble.cpp - BLE client implementation for Alber e-motion M25 wheels
 *
 * Implementation file for the M25 SPP-over-BLE communication stack.
 * See m25_ble.h for protocol documentation and API reference.
 */

#include "m25_ble.h"

// ---------------------------------------------------------------------------
// Internal static storage (single instance across translation units)
// ---------------------------------------------------------------------------

static WheelConnState_t _wheelsStorage[WHEEL_COUNT];
static bool _bleAutoReconnectFlag = true;

WheelConnState_t* _getWheels() {
    return _wheelsStorage;
}

bool& _getBleAutoReconnect() {
    return _bleAutoReconnectFlag;
}

// ---------------------------------------------------------------------------
// Disconnect callback instances
// ---------------------------------------------------------------------------

static M25DisconnectCallback _callbacks[WHEEL_COUNT];

// ---------------------------------------------------------------------------
// CRC-16 implementation
// ---------------------------------------------------------------------------

uint16_t _m25Crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ pgm_read_word(&_crcTable[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Byte stuffing / unstuffing
// ---------------------------------------------------------------------------

size_t _addDelimiters(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];
    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        if (in[i] == M25_HEADER_MARKER) out[pos++] = M25_HEADER_MARKER;
    }
    return pos;
}

size_t _removeDelimiters(const uint8_t* in, size_t inLen, uint8_t* out, size_t outMax) {
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
// NACK error code interpretation
// ---------------------------------------------------------------------------

const char* _nackCodeToString(uint8_t code) {
    switch (code) {
        case M25_NACK_GENERAL:            return "General error";
        case M25_NACK_SID:                return "Invalid service ID";
        case M25_NACK_PID:                return "Invalid parameter ID";
        case M25_NACK_LENGTH:             return "Invalid length";
        case M25_NACK_CHKSUM:             return "Checksum error";
        case M25_NACK_COND:               return "Condition not met";
        case M25_NACK_SEC_ACC:            return "Security/access denied";
        case M25_NACK_CMD_NOT_EXEC:       return "Command not executed";
        case M25_NACK_CMD_INTERNAL_ERROR: return "Internal error";
        default:                          return "Unknown NACK";
    }
}

bool _isNack(uint8_t paramId) {
    return paramId >= M25_NACK_GENERAL && paramId <= M25_NACK_CMD_INTERNAL_ERROR;
}

bool _isAck(uint8_t paramId) {
    return paramId == M25_PARAM_ACK;
}

// ---------------------------------------------------------------------------
// Wheel activity filter
// ---------------------------------------------------------------------------

bool _wheelActive(int idx) {
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
// Encryption / Decryption
// ---------------------------------------------------------------------------

bool _m25Encrypt(const uint8_t* key, const uint8_t* spp, uint8_t sppLen,
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

bool _m25Decrypt(const uint8_t* key, const uint8_t* frame, size_t frameLen,
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
// SPP packet building and sending
// ---------------------------------------------------------------------------

size_t _buildAndEncrypt(int idx, uint8_t serviceId, uint8_t paramId,
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

bool _sendCommand(int idx, uint8_t serviceId, uint8_t paramId,
                  const uint8_t* payload, uint8_t payloadLen) {
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
// Response parsing
// ---------------------------------------------------------------------------

bool _parseResponseHeader(const uint8_t* spp, size_t sppLen, ResponseHeader* hdr) {
    if (!spp || !hdr || sppLen < 6) {
        return false;
    }
    
    hdr->protocolId = spp[0];
    hdr->telegramId = spp[1];
    hdr->sourceId   = spp[2];
    hdr->destId     = spp[3];
    hdr->serviceId  = spp[4];
    hdr->paramId    = spp[5];
    hdr->payload    = (sppLen > 6) ? (spp + 6) : nullptr;
    hdr->payloadLen = (sppLen > 6) ? (sppLen - 6) : 0;
    
    return true;
}

bool _parseResponseData(const ResponseHeader* hdr, ResponseData* data) {
    if (!hdr || !data) return false;
    
    memset(data, 0, sizeof(ResponseData));
    
    // Check for ACK/NACK
    data->isAck = _isAck(hdr->paramId);
    data->isNack = _isNack(hdr->paramId);
    
    if (data->isNack) {
        data->nackCode = hdr->paramId;
        return true;
    }
    
    if (data->isAck) {
        return true;  // Simple ACK with no payload
    }
    
    const uint8_t* p = hdr->payload;
    size_t len = hdr->payloadLen;
    
    // Parse based on service and parameter ID
    if (hdr->serviceId == M25_SRV_BATT_MGMT) {
        if (hdr->paramId == M25_PARAM_STATUS_SOC && len >= 1) {
            data->soc.batteryPercent = _parseUint8(p, 0);
            return true;
        }
    }
    else if (hdr->serviceId == M25_SRV_APP_MGMT) {
        if (hdr->paramId == M25_PARAM_STATUS_ASSIST_LEVEL && len >= 1) {
            data->assistLevel.level = _parseUint8(p, 0);
            return true;
        }
        else if (hdr->paramId == M25_PARAM_STATUS_DRIVE_MODE && len >= 1) {
            data->driveMode.mode = _parseUint8(p, 0);
            data->driveMode.autoHold = (data->driveMode.mode & M25_DRIVE_MODE_AUTO_HOLD) != 0;
            data->driveMode.cruise = (data->driveMode.mode & M25_DRIVE_MODE_CRUISE) != 0;
            data->driveMode.remote = (data->driveMode.mode & M25_DRIVE_MODE_REMOTE) != 0;
            return true;
        }
        else if (hdr->paramId == M25_PARAM_CRUISE_VALUES && len >= 12) {
            // Parse cruise values: distance (4 bytes BE), speed (2 bytes BE), push counter (2 bytes BE)
            data->cruiseValues.distanceMm = _parseUint32BE(p, 0);
            data->cruiseValues.distanceKm = (float)data->cruiseValues.distanceMm * 0.00001f;  // 0.01mm to km
            data->cruiseValues.speed = _parseUint16BE(p, 4);
            data->cruiseValues.pushCounter = _parseUint16BE(p, 6);
            return true;
        }
        // Also handle write command echoes (ACKs with payload)
        else if (hdr->paramId == M25_PARAM_WRITE_SYSTEM_MODE && len >= 1) {
            // System mode ACK - echoes back the mode value
            return true;
        }
        else if (hdr->paramId == M25_PARAM_WRITE_DRIVE_MODE && len >= 1) {
            // Drive mode ACK - echoes back the mode bits
            data->driveMode.mode = _parseUint8(p, 0);
            data->driveMode.autoHold = (data->driveMode.mode & M25_DRIVE_MODE_AUTO_HOLD) != 0;
            data->driveMode.cruise = (data->driveMode.mode & M25_DRIVE_MODE_CRUISE) != 0;
            data->driveMode.remote = (data->driveMode.mode & M25_DRIVE_MODE_REMOTE) != 0;
            return true;
        }
        else if (hdr->paramId == M25_PARAM_WRITE_REMOTE_SPEED && len >= 2) {
            // Speed ACK - echoes back the speed value
            return true;
        }
    }
    else if (hdr->serviceId == M25_SRV_VERSION_MGMT) {
        if (hdr->paramId == M25_PARAM_STATUS_SW_VERSION && len >= 4) {
            data->swVersion.devState = _parseUint8(p, 0);
            data->swVersion.major = _parseUint8(p, 1);
            data->swVersion.minor = _parseUint8(p, 2);
            data->swVersion.patch = _parseUint8(p, 3);
            return true;
        }
    }
    
    return false;  // Unknown response type
}

void _printResponse(const char* wheelName, const ResponseHeader* hdr, const ResponseData* data) {
    if (!wheelName || !hdr) return;
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] %s wheel response:\n", wheelName);
        Serial.printf("  Protocol: 0x%02X, Telegram: 0x%02X\n", hdr->protocolId, hdr->telegramId);
        Serial.printf("  Src: 0x%02X, Dest: 0x%02X\n", hdr->sourceId, hdr->destId);
        Serial.printf("  Service: 0x%02X, Param: 0x%02X\n", hdr->serviceId, hdr->paramId);
        
        if (hdr->payloadLen > 0) {
            Serial.printf("  Payload (%zu bytes): ", hdr->payloadLen);
            for (size_t i = 0; i < hdr->payloadLen && i < 16; i++) {
                Serial.printf("%02X ", hdr->payload[i]);
            }
            if (hdr->payloadLen > 16) Serial.print("...");
            Serial.println();
        }
    }
    
    if (!data) return;
    
    // Interpret response
    if (data->isNack) {
        Serial.printf("[BLE] %s wheel NACK: 0x%02X - %s\n",
                     wheelName, data->nackCode, _nackCodeToString(data->nackCode));
    }
    else if (data->isAck) {
        if (debugFlags & DBG_BLE) {
            Serial.printf("[BLE] %s wheel ACK\n", wheelName);
        }
    }
    else {
        // Print parsed data based on response type
        if (hdr->serviceId == M25_SRV_BATT_MGMT && hdr->paramId == M25_PARAM_STATUS_SOC) {
            Serial.printf("[BLE] %s wheel battery: %d%%\n", wheelName, data->soc.batteryPercent);
        }
        else if (hdr->serviceId == M25_SRV_APP_MGMT) {
            if (hdr->paramId == M25_PARAM_STATUS_ASSIST_LEVEL) {
                const char* levelName = (data->assistLevel.level == 0) ? "Indoor" :
                                       (data->assistLevel.level == 1) ? "Outdoor" :
                                       (data->assistLevel.level == 2) ? "Learning" : "Unknown";
                Serial.printf("[BLE] %s wheel assist level: %s\n", wheelName, levelName);
            }
            else if (hdr->paramId == M25_PARAM_STATUS_DRIVE_MODE || 
                     hdr->paramId == M25_PARAM_WRITE_DRIVE_MODE) {
                Serial.printf("[BLE] %s wheel drive mode: 0x%02X (", wheelName, data->driveMode.mode);
                if (data->driveMode.remote) Serial.print("REMOTE ");
                if (data->driveMode.cruise) Serial.print("CRUISE ");
                if (data->driveMode.autoHold) Serial.print("AUTO_HOLD ");
                if (data->driveMode.mode == 0) Serial.print("NORMAL");
                Serial.println(")");
            }
            else if (hdr->paramId == M25_PARAM_CRUISE_VALUES) {
                Serial.printf("[BLE] %s wheel cruise: %.2f km, speed %d, push %d\n",
                             wheelName, data->cruiseValues.distanceKm,
                             data->cruiseValues.speed, data->cruiseValues.pushCounter);
            }
        }
        else if (hdr->serviceId == M25_SRV_VERSION_MGMT && hdr->paramId == M25_PARAM_STATUS_SW_VERSION) {
            Serial.printf("[BLE] %s wheel firmware: %d.%d.%d (dev state %d)\n",
                         wheelName, data->swVersion.major, data->swVersion.minor,
                         data->swVersion.patch, data->swVersion.devState);
        }
    }
}

void _parseSppPacket(const uint8_t* spp, size_t sppLen, const char* wheelName) {
    ResponseHeader hdr;
    if (!_parseResponseHeader(spp, sppLen, &hdr)) {
        Serial.printf("[BLE] %s wheel: Failed to parse SPP header\n", wheelName);
        return;
    }
    
    ResponseData data;
    bool parsed = _parseResponseData(&hdr, &data);
    
    if (!parsed && debugFlags & DBG_BLE) {
        Serial.printf("[BLE] %s wheel: Unknown response type (srv=0x%02X, param=0x%02X)\n",
                     wheelName, hdr.serviceId, hdr.paramId);
    }
    
    _printResponse(wheelName, &hdr, parsed ? &data : nullptr);
}

// ---------------------------------------------------------------------------
// BLE notification callback
// ---------------------------------------------------------------------------

void _notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
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
// Disconnect callback implementation
// ---------------------------------------------------------------------------

void M25DisconnectCallback::onConnect(BLEClient*) {}

void M25DisconnectCallback::onDisconnect(BLEClient*) {
    _wheels[wheelIdx].connected     = false;
    _wheels[wheelIdx].protocolReady = false;
    _wheels[wheelIdx].driveModeBits = 0;
    
    // Only print message for active wheels (respect WHEEL_MODE)
    if (_wheelActive(wheelIdx)) {
        Serial.printf("[BLE] %s wheel disconnected\n", _wheels[wheelIdx].name);
    }
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

bool _connectWheel(int idx) {
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
        // registerForNotify() calls retrieveDescriptors() internally.  On cold
        // boot the ESP32 GATT client may still be settling, causing
        // esp_ble_gattc_get_all_descr to return Unknown even though connect()
        // succeeded.  When that happens the notification callback is silently
        // never registered. We call it twice with a delay to improve reliability
        // on cold boot scenarios.
        w.txChar->registerForNotify(_notifyCallback);
        Serial.printf("[BLE] %s wheel: Notifications registered, waiting %d ms for stability...\n",
                      wheelName, BLE_NOTIFY_RETRY_DELAY_MS);
        delay(BLE_NOTIFY_RETRY_DELAY_MS);
        w.txChar->registerForNotify(_notifyCallback);
        Serial.printf("[BLE] %s wheel: Notifications enabled\n", wheelName);
    }
    w.receivedFirstAck = false;  // Reset ACK flag

    w.connected = true;

    // Delay to ensure BLE GATT is fully established and wheel is ready to respond
    // Increased from 50ms to 200ms to improve first-connection reliability
    delay(BLE_POST_GATT_DELAY_MS);

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
// Public API implementations
// ---------------------------------------------------------------------------

void bleInit(const char* deviceName) {
    // Populate mutable wheel config from compile-time device_config.h defaults
    memset(_wheels, 0, sizeof(_wheelsStorage));
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] Wheels array after memset: %p (size: %d bytes)\n", 
                      (void*)_wheels, (int)sizeof(_wheelsStorage));
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
    
#if WHEEL_MODE == WHEEL_MODE_DUAL
    Serial.println("================================================================================");
    Serial.println("[BLE] WARNING: Dual wheel mode requires ESP32 BLE configuration change!");
    Serial.println("[BLE] Default ESP32 supports only 1 GATT client connection.");
    Serial.println("[BLE] See BLE_CONFIGURATION.md for setup instructions.");
    Serial.println("[BLE] Symptoms: Wheels disconnect when 2nd wheel connects.");
    Serial.println("[BLE] Solution: Use PlatformIO with CONFIG_GATTC_MAX_CONNECTIONS=2");
    Serial.println("================================================================================");
#endif
    
    // Allow the ESP32 BLE GATT client stack to fully initialize before any
    // connection attempt.  BLEDevice::init() triggers the BT controller and
    // host stack asynchronously; 100 ms is not enough - on cold boot the first
    // retrieveDescriptors() call fails with ESP_GATT_UNKNOWN, the notification
    // callback is never registered, and SYSTEM_MODE validation times out.
    // BLE_STACK_INIT_DELAY_MS (700 ms) gives the stack time to stabilize.
    // RNG entropy also improves over this window, so the existing RNG test
    // below still runs afterwards.
    delay(BLE_STACK_INIT_DELAY_MS);
    
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

void bleConnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && !_wheels[i].connected) {
            if (debugFlags & DBG_BLE) {
                Serial.printf("[BLE] bleConnect: About to connect wheel %d, MAC='%s' (len=%d)\n",
                              i, _wheels[i].mac, (int)strlen(_wheels[i].mac));
            }
            _connectWheel(i);
            
            // Add delay between wheel connections to prevent BLE stack contention
            // and give each wheel time to fully initialize before connecting the next
            if (i < WHEEL_COUNT - 1 && _wheelActive(i + 1)) {
                delay(BLE_INTER_WHEEL_DELAY_MS);
            }
        }
    }
}

void bleDisconnect() {
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

bool bleIsConnected(int wheelIdx) {
    WheelConnState_t &w = _wheels[wheelIdx];
    return w.connected && w.protocolReady
        && w.client != nullptr && w.client->isConnected();
}

bool bleAllConnected() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && !bleIsConnected(i)) return false;
    }
    return true;
}

bool bleAnyConnected() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && bleIsConnected(i)) return true;
    }
    return false;
}

bool bleSendStop() {
    uint8_t spd[2] = { 0, 0 };
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
        }
    }
    return ok;
}

bool bleSendMotorCommand(float leftPercent, float rightPercent) {
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

bool bleSendHillHold(bool enable) {
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

bool bleSendAssistLevel(uint8_t level) {
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

void bleTick() {
    if (!_bleAutoReconnect) return;
    uint32_t now = millis();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        WheelConnState_t &w = _wheels[i];
        if (!w.connected) {
            if (now - w.lastConnectAttemptMs >= BLE_RECONNECT_DELAY_MS) {
                w.lastConnectAttemptMs = now;
                if (debugFlags & DBG_BLE) {
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

void bleSetAutoReconnect(bool enable) {
    _bleAutoReconnect = enable;
    if (enable) {
        // Reset failure counters so the fresh run gets a clean slate
        for (int i = 0; i < WHEEL_COUNT; i++) {
            _wheels[i].consecutiveFails = 0;
        }
    }
    Serial.printf("[BLE] Auto-reconnect: %s\n", enable ? "ON" : "off");
}

bool bleGetAutoReconnect() {
    return _bleAutoReconnect;
}

void bleSetMac(int idx, const char* mac) {
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
}

void bleSetKey(int idx, const uint8_t* newKey) {
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
    
    WheelConnState_t &w = _wheels[idx];
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] _wheels[%d].key address: %p\n", idx, (void*)w.key);
        Serial.printf("[BLE] _wheels[%d].mac address: %p\n", idx, (void*)w.mac);
        Serial.printf("[BLE] _wheels[%d].name before: %s\n", idx, w.name ? w.name : "NULL");
        Serial.printf("[BLE] _wheels[%d].mac before: '%s' (len=%d)\n", idx, w.mac, (int)strlen(w.mac));
    }
    
    memcpy(w.key, newKey, 16);
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[BLE] _wheels[%d].name after: %s\n", idx, w.name ? w.name : "NULL");
        Serial.printf("[BLE] _wheels[%d].mac after: '%s' (len=%d)\n", idx, w.mac, (int)strlen(w.mac));
    }
    
    if (w.name) {
        Serial.printf("[BLE] %s wheel key updated  (reconnect required)\n", w.name);
    } else {
        Serial.printf("[BLE] Wheel %d key updated  (reconnect required)\n", idx);
    }
}

void blePrintWheelDetails() {
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
