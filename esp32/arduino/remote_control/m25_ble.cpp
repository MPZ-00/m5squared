/*
 * m25_ble.cpp - BLE client implementation for Alber e-motion M25 wheels
 *
 * Implementation file for the M25 SPP-over-BLE communication stack.
 * See m25_ble.h for protocol documentation and API reference.
 */

#include "m25_ble.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_bt_defs.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <esp_spp_api.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <esp_bt.h>
#include <esp_bt_main.h>

// ---------------------------------------------------------------------------
// Internal static storage (single instance across translation units)
// ---------------------------------------------------------------------------

static WheelConnState_t _wheelsStorage[WHEEL_COUNT];
static bool _bleAutoReconnectFlag = true;

// Mutex serializing all GATT writes across tasks.
// The BLE GATT client is not thread-safe: concurrent writeValue() calls from
// different RTOS tasks (motor task on Core 0, telemetry on Core 1, connect
// init on Core 0) produce "Unknown ESP_ERR" and corrupt the GATT state.
// Every _sendCommand() caller must hold this before touching the BLE stack.
static SemaphoreHandle_t _bleTxMutex = nullptr;

// Motor STOP debug log filter (used only when DBG_MOTOR is enabled).
static volatile bool _motorStopLogEnabled = true;
static volatile uint16_t _motorStopLogEvery = 20;

// TX statistics for diagnosing what we actually send to the wheels.
struct TxStats {
    uint32_t totalAttempts;
    uint32_t totalSuccess;
    uint32_t totalFail;
    uint32_t remoteSpeed;
    uint32_t remoteSpeedStop;
    uint32_t remoteSpeedMotion;
    uint32_t driveMode;
    uint32_t assistLevel;
    uint32_t readSoc;
    uint32_t readFw;
    uint32_t readCruise;
    uint32_t other;
};

static TxStats _txStats = {};
static portMUX_TYPE _txStatsMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// BLE traffic recorder state
// ---------------------------------------------------------------------------
static BleRecordEntry    _recordBuf[BLE_RECORD_MAX];
static volatile uint16_t _recordCount  = 0;
static volatile bool     _recordActive = false;
static volatile uint32_t _recordEndMs  = 0;   // millis() when auto-stop fires
static portMUX_TYPE      _recordMux    = portMUX_INITIALIZER_UNLOCKED;

WheelConnState_t* _getWheels() {
    return _wheelsStorage;
}

bool& _getBleAutoReconnect() {
    return _bleAutoReconnectFlag;
}

// ---------------------------------------------------------------------------
// BLE traffic recorder implementation
// ---------------------------------------------------------------------------
void _bleRecordFrame(uint8_t dir, uint8_t wheelIdx, const uint8_t* data, size_t len) {
    if (!_recordActive) return;
    portENTER_CRITICAL(&_recordMux);
    if (_recordActive && _recordCount < BLE_RECORD_MAX) {
        uint16_t idx        = _recordCount;
        BleRecordEntry& e   = _recordBuf[idx];
        e.ms        = millis();
        e.direction = dir;
        e.wheel     = wheelIdx;
        e.rawLen    = (uint8_t)min(len, (size_t)255);
        size_t copy = (len < BLE_RECORD_PAYLOAD) ? len : BLE_RECORD_PAYLOAD;
        memcpy(e.data, data, copy);
        if (copy < BLE_RECORD_PAYLOAD)
            memset(e.data + copy, 0, BLE_RECORD_PAYLOAD - copy);
        _recordCount = idx + 1;
    }
    portEXIT_CRITICAL(&_recordMux);
}

void bleRecordStart(uint32_t durationMs) {
    portENTER_CRITICAL(&_recordMux);
    memset(_recordBuf, 0, sizeof(_recordBuf));
    _recordCount  = 0;
    _recordActive = true;
    _recordEndMs  = millis() + durationMs;
    portEXIT_CRITICAL(&_recordMux);
    Serial.printf("[Record] Started - %.1f s / %d entries max\n",
                  durationMs / 1000.0f, BLE_RECORD_MAX);
}

void bleRecordStop() {
    portENTER_CRITICAL(&_recordMux);
    _recordActive = false;
    portEXIT_CRITICAL(&_recordMux);
    Serial.printf("[Record] Stopped - %d entries captured\n", (int)_recordCount);
}

bool bleRecordIsActive() {
    return _recordActive;
}

uint32_t bleRecordEntryCount() {
    return _recordCount;
}

void bleRecordDump() {
    uint16_t count = _recordCount;
    if (count == 0) {
        Serial.println("[Record] No entries captured");
        return;
    }
    uint32_t t0 = _recordBuf[0].ms;
    Serial.printf("[Record] %d entries captured (t0=%u ms since boot)\n", (int)count, t0);
    Serial.println("[Record]  idx   +ms     dir  wheel  len  data");
    Serial.println("[Record] ----  ------   ---  -----  ---  ----");
    for (uint16_t i = 0; i < count; i++) {
        const BleRecordEntry& e = _recordBuf[i];
        const char* dir   = (e.direction == BLE_REC_TX) ? "TX>" : "<RX";
        const char* wheel = (e.wheel == WHEEL_LEFT)  ? "Left " :
                            (e.wheel == WHEEL_RIGHT) ? "Right" : "?    ";
        Serial.printf("[Record] %-4u  +%-6u  %s  %s  %-3d  ",
                      i, (uint32_t)(e.ms - t0), dir, wheel, e.rawLen);
        size_t show = (e.rawLen < BLE_RECORD_PAYLOAD) ? e.rawLen : BLE_RECORD_PAYLOAD;
        for (size_t j = 0; j < show; j++) {
            Serial.printf("%02X ", e.data[j]);
        }
        if (e.rawLen > BLE_RECORD_PAYLOAD) {
            Serial.printf("... (+%d)", e.rawLen - BLE_RECORD_PAYLOAD);
        }
        Serial.println();
    }
    Serial.println("[Record] --- end of record ---");
}

// ---------------------------------------------------------------------------
// Disconnect callback instances
// ---------------------------------------------------------------------------
#if M25_TRANSPORT_BLE
static M25DisconnectCallback _callbacks[WHEEL_COUNT];
#endif

#if M25_TRANSPORT_RFCOMM
static bool _rfcommInitDone = false;
static volatile bool _rfcommReady = false;
static volatile int _rfcommPendingIdx = -1;
static bool _rfcommCbRegistered = false;
static bool _rfcommGapCbRegistered = false;
static bool _rfcommSppInitRequested = false;
static volatile bool _rfcommOpenEvt[WHEEL_COUNT] = { false, false };
static volatile bool _rfcommCloseEvt[WHEEL_COUNT] = { false, false };
static volatile int  _rfcommOpenStatus[WHEEL_COUNT] = { -1, -1 };
static uint8_t _rfRxBuf[WHEEL_COUNT][256];
static size_t  _rfRxLen[WHEEL_COUNT] = { 0, 0 };
#endif

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

// Translate BLE/GATT error codes to strings.
// esp_err_to_name() does not cover GATT status codes (esp_gatt_status_t),
// so the library logs them as "Unknown ESP_ERR error". This covers the codes
// we actually encounter.
static const char* _bleErrStr(int rc) {
    switch (rc) {
        case  0:     return "ESP_OK";
        case -1:     return "ESP_FAIL (stack internal error)";
        case 0x101:  return "ESP_ERR_NO_MEM";
        case 0x102:  return "ESP_ERR_INVALID_ARG";
        case 0x103:  return "ESP_ERR_INVALID_STATE (259) - write during teardown";
        case 0x104:  return "ESP_ERR_INVALID_SIZE";
        case 0x105:  return "ESP_ERR_NOT_FOUND";
        case 0x106:  return "ESP_ERR_NOT_SUPPORTED";
        case 0x107:  return "ESP_ERR_TIMEOUT";
        // esp_gatt_status_t (NOT in esp_err_to_name - logged as "Unknown ESP_ERR")
        case 0x01:   return "ESP_GATT_INVALID_HANDLE";
        case 0x02:   return "ESP_GATT_READ_NOT_PERMIT";
        case 0x03:   return "ESP_GATT_WRITE_NOT_PERMIT";
        case 0x06:   return "ESP_GATT_INVALID_PDU";
        case 0x08:   return "ESP_GATT_INSUF_AUTHORIZATION";
        case 0x0A:   return "ESP_GATT_INVALID_OFFSET";
        case 0x0D:   return "ESP_GATT_INVALID_ATTR_LEN";
        case 0x13:   return "ESP_GATT_UNKNOWN (19) - no descriptors (benign on first connect)";
        case 0x85:   return "ESP_GATT_ERROR (133) - not connectable / controller busy";
        case 0x8D:   return "ESP_GATT_BUSY";
        case 0x8E:   return "ESP_GATT_ERROR (generic)";
        case 0x8F:   return "ESP_GATT_CMD_STARTED";
        case 0x96:   return "ESP_GATT_AUTH_FAIL";
        case 0xFF:   return "connect scan timeout (255) - wheel not advertising or out of range";
        default: {
            static char buf[24];
            snprintf(buf, sizeof(buf), "0x%X (%d)", rc, rc);
            return buf;
        }
    }
}

#if M25_TRANSPORT_RFCOMM
static int _findWheelByBda(const uint8_t* bda) {
    if (!bda) return -1;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (memcmp(_wheels[i].bda, bda, ESP_BD_ADDR_LEN) == 0) return i;
    }
    return -1;
}

static int _findWheelByHandle(uint32_t handle) {
    if (handle == 0) return -1;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheels[i].sppHandle == handle) return i;
    }
    return -1;
}

static const char* _sppEvtName(esp_spp_cb_event_t ev) {
    switch (ev) {
        case ESP_SPP_INIT_EVT: return "INIT";
        case ESP_SPP_DISCOVERY_COMP_EVT: return "DISCOVERY_COMP";
        case ESP_SPP_OPEN_EVT: return "OPEN";
        case ESP_SPP_CLOSE_EVT: return "CLOSE";
        case ESP_SPP_START_EVT: return "START";
        case ESP_SPP_CL_INIT_EVT: return "CL_INIT";
        case ESP_SPP_DATA_IND_EVT: return "DATA_IND";
        case ESP_SPP_CONG_EVT: return "CONG";
        case ESP_SPP_WRITE_EVT: return "WRITE";
        case ESP_SPP_SRV_OPEN_EVT: return "SRV_OPEN";
        default: return "OTHER";
    }
}

static void _rfcommGapCb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    if (!(debugFlags & DBG_BT_AUTH)) return;
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            Serial.printf("[AUTH] AUTH_CMPL status=%d addr=%02X:%02X:%02X:%02X:%02X:%02X name=%s\n",
                          param->auth_cmpl.stat,
                          param->auth_cmpl.bda[0], param->auth_cmpl.bda[1],
                          param->auth_cmpl.bda[2], param->auth_cmpl.bda[3],
                          param->auth_cmpl.bda[4], param->auth_cmpl.bda[5],
                          (const char*)param->auth_cmpl.device_name);
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            Serial.printf("[AUTH] PIN_REQ min16=%d\n", param->pin_req.min_16_digit);
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            Serial.printf("[AUTH] CFM_REQ num=%lu\n", (unsigned long)param->cfm_req.num_val);
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            Serial.printf("[AUTH] KEY_NOTIF passkey=%lu\n", (unsigned long)param->key_notif.passkey);
            break;
        case ESP_BT_GAP_KEY_REQ_EVT:
            Serial.println("[AUTH] KEY_REQ");
            break;
        default:
            Serial.printf("[AUTH] GAP event=%d\n", (int)event);
            break;
    }
}

static bool _parseMacToBda(const char* mac, esp_bd_addr_t out) {
    if (!mac || !out) return false;
    unsigned v[6];
    if (sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

static void _rfcommConsumeBuffered(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return;
    while (_rfRxLen[idx] >= 5) {
        size_t start = 0;
        while (start < _rfRxLen[idx] && _rfRxBuf[idx][start] != M25_HEADER_MARKER) start++;
        if (start > 0) {
            memmove(_rfRxBuf[idx], _rfRxBuf[idx] + start, _rfRxLen[idx] - start);
            _rfRxLen[idx] -= start;
            if (_rfRxLen[idx] < 5) return;
        }

        uint8_t unstuffed[128];
        size_t uPos = 0;
        size_t sPos = 0;
        while (sPos < _rfRxLen[idx] && uPos < 3) {
            unstuffed[uPos++] = _rfRxBuf[idx][sPos++];
            if (uPos > 1 && unstuffed[uPos - 1] == M25_HEADER_MARKER &&
                sPos < _rfRxLen[idx] && _rfRxBuf[idx][sPos] == M25_HEADER_MARKER) {
                sPos++;
            }
        }
        if (uPos < 3) return;

        uint16_t frameField = ((uint16_t)unstuffed[1] << 8) | unstuffed[2];
        size_t totalU = (size_t)frameField + 1;
        if (totalU > sizeof(unstuffed)) {
            memmove(_rfRxBuf[idx], _rfRxBuf[idx] + 1, _rfRxLen[idx] - 1);
            _rfRxLen[idx]--;
            continue;
        }

        while (sPos < _rfRxLen[idx] && uPos < totalU) {
            unstuffed[uPos++] = _rfRxBuf[idx][sPos++];
            if (uPos > 1 && unstuffed[uPos - 1] == M25_HEADER_MARKER &&
                sPos < _rfRxLen[idx] && _rfRxBuf[idx][sPos] == M25_HEADER_MARKER) {
                sPos++;
            }
        }
        if (uPos < totalU) return;

        _bleRecordFrame(BLE_REC_RX, (uint8_t)idx, _rfRxBuf[idx], sPos);
        _wheels[idx].lastNotifyMs = millis();

        uint8_t sppPacket[64];
        size_t sppLen = 0;
        if (_m25Decrypt(_wheels[idx].key, unstuffed, uPos, sppPacket, &sppLen)) {
            if (!_wheels[idx].receivedFirstAck) {
                _wheels[idx].receivedFirstAck = true;
                Serial.printf("[RFCOMM] %s wheel: First response received (%zu bytes)\n",
                              _wheels[idx].name ? _wheels[idx].name : "Unknown", sPos);
            }
            _parseSppPacket(sppPacket, sppLen, idx);
        } else if (debugFlags & DBG_BLE) {
            Serial.printf("[RFCOMM] %s wheel: Decryption failed\n",
                          _wheels[idx].name ? _wheels[idx].name : "Unknown");
        }

        memmove(_rfRxBuf[idx], _rfRxBuf[idx] + sPos, _rfRxLen[idx] - sPos);
        _rfRxLen[idx] -= sPos;
    }
}

static void _rfcommSppCb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    if (!param) return;
    if (debugFlags & DBG_BT_AUTH) {
        Serial.printf("[AUTH] SPP event=%s(%d)\n", _sppEvtName(event), (int)event);
    }
    switch (event) {
        case ESP_SPP_INIT_EVT:
            if (param->init.status == ESP_SPP_SUCCESS) {
                _rfcommReady = true;
                Serial.println("[RFCOMM] SPP stack ready");
            } else {
                Serial.printf("[RFCOMM] SPP init failed: status=%d\n", (int)param->init.status);
            }
            break;
        case ESP_SPP_OPEN_EVT: {
            int idx = _rfcommPendingIdx;
            if (idx < 0 || idx >= WHEEL_COUNT) {
                idx = _findWheelByHandle(param->open.handle);
            }
            if (idx >= 0) {
                _wheels[idx].sppHandle = param->open.handle;
                _wheels[idx].connected = true;
                _rfcommOpenStatus[idx] = param->open.status;
                _rfcommOpenEvt[idx] = true;
                _rfcommPendingIdx = -1;
                Serial.printf("[RFCOMM] %s wheel link open (status=%d, handle=%u)\n",
                              _wheels[idx].name ? _wheels[idx].name : "?",
                              (int)param->open.status,
                              (unsigned)param->open.handle);
            }
            break;
        }
        case ESP_SPP_CLOSE_EVT: {
            int idx = _findWheelByHandle(param->close.handle);
            if (idx >= 0) {
                _wheels[idx].connected = false;
                _wheels[idx].protocolReady = false;
                _wheels[idx].driveModeBits = 0;
                _wheels[idx].sppHandle = 0;
                _rfRxLen[idx] = 0;
                _rfcommCloseEvt[idx] = true;
                Serial.printf("[RFCOMM] %s wheel disconnected\n",
                              _wheels[idx].name ? _wheels[idx].name : "?");
            }
            break;
        }
        case ESP_SPP_DATA_IND_EVT: {
            int idx = _findWheelByHandle(param->data_ind.handle);
            if (idx < 0) break;
            size_t copy = param->data_ind.len;
            if (copy > 0 && param->data_ind.data) {
                if (_rfRxLen[idx] + copy > sizeof(_rfRxBuf[idx])) {
                    size_t overflow = (_rfRxLen[idx] + copy) - sizeof(_rfRxBuf[idx]);
                    if (overflow >= _rfRxLen[idx]) {
                        _rfRxLen[idx] = 0;
                    } else {
                        memmove(_rfRxBuf[idx], _rfRxBuf[idx] + overflow, _rfRxLen[idx] - overflow);
                        _rfRxLen[idx] -= overflow;
                    }
                }
                memcpy(_rfRxBuf[idx] + _rfRxLen[idx], param->data_ind.data, copy);
                _rfRxLen[idx] += copy;
                _rfcommConsumeBuffered(idx);
            }
            break;
        }
        default:
            break;
    }
}
#endif

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
                        uint8_t* out,
                        uint8_t* sppOut,
                        uint8_t* sppLenOut) {
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

    if (sppOut) {
        memcpy(sppOut, spp, n);
    }
    if (sppLenOut) {
        *sppLenOut = n;
    }

    size_t encLen = 0;
    if (!_m25Encrypt(w.key, spp, n, out, &encLen)) return 0;
    return encLen;
}

static const char* _wheelNameOrDefault(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return "?";
    const char* name = _wheels[idx].name;
    return name ? name : "?";
}

static const char* _paramName(uint8_t serviceId, uint8_t paramId) {
    if (serviceId == M25_SRV_APP_MGMT) {
        switch (paramId) {
            case M25_PARAM_WRITE_SYSTEM_MODE:  return "WRITE_SYSTEM_MODE";
            case M25_PARAM_WRITE_DRIVE_MODE:   return "WRITE_DRIVE_MODE";
            case M25_PARAM_WRITE_REMOTE_SPEED: return "WRITE_REMOTE_SPEED";
            case M25_PARAM_WRITE_ASSIST_LEVEL: return "WRITE_ASSIST_LEVEL";
            case M25_PARAM_READ_ASSIST_LEVEL:  return "READ_ASSIST_LEVEL";
            case M25_PARAM_READ_DRIVE_MODE:    return "READ_DRIVE_MODE";
            case M25_PARAM_READ_CRUISE_VALUES: return "READ_CRUISE_VALUES";
            default:                           return "APP_MGMT";
        }
    }
    if (serviceId == M25_SRV_BATT_MGMT) {
        return (paramId == M25_PARAM_READ_SOC) ? "READ_SOC" : "BATT_MGMT";
    }
    if (serviceId == M25_SRV_VERSION_MGMT) {
        return (paramId == M25_PARAM_READ_SW_VERSION) ? "READ_SW_VERSION" : "VERSION_MGMT";
    }
    return "UNKNOWN";
}

static void _printHexBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        Serial.print("<empty>");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (i > 0) Serial.print(' ');
        Serial.printf("%02X", data[i]);
    }
}

static void _debugLogTxPacket(int idx,
                              uint8_t serviceId,
                              uint8_t paramId,
                              const uint8_t* payload,
                              uint8_t payloadLen,
                              const uint8_t* spp,
                              uint8_t sppLen,
                              const uint8_t* wire,
                              size_t wireLen) {
    const char* wheelName = _wheelNameOrDefault(idx);
    const char* paramName = _paramName(serviceId, paramId);
    uint8_t telegramId = (sppLen >= 2) ? spp[1] : 0;

    Serial.printf("[TX] %s %s svc=0x%02X param=0x%02X tg=0x%02X payloadLen=%u\n",
                  wheelName, paramName, serviceId, paramId, telegramId, payloadLen);

    if (serviceId == M25_SRV_APP_MGMT && paramId == M25_PARAM_WRITE_DRIVE_MODE && payloadLen >= 1) {
        uint8_t mode = payload[0];
        Serial.printf("[TX]   drive_mode=0x%02X remote=%u cruise=%u auto_hold=%u\n",
                      mode,
                      (mode & M25_DRIVE_MODE_REMOTE) ? 1 : 0,
                      (mode & M25_DRIVE_MODE_CRUISE) ? 1 : 0,
                      (mode & M25_DRIVE_MODE_AUTO_HOLD) ? 1 : 0);
    } else if (serviceId == M25_SRV_APP_MGMT && paramId == M25_PARAM_WRITE_REMOTE_SPEED && payloadLen >= 2) {
        int16_t raw = (int16_t)(((uint16_t)payload[0] << 8) | payload[1]);
        float pct = (float)raw / M25_SPEED_SCALE;
        Serial.printf("[TX]   remote_speed raw=%d pct=%+.1f payload=%02X %02X\n",
                      (int)raw, (double)pct, payload[0], payload[1]);
    } else if (payloadLen > 0) {
        Serial.print("[TX]   payload: ");
        _printHexBytes(payload, payloadLen);
        Serial.println();
    }

    Serial.print("[TX]   spp   : ");
    _printHexBytes(spp, sppLen);
    Serial.println();
    Serial.print("[TX]   wire  : ");
    _printHexBytes(wire, wireLen);
    Serial.println();
}

// Returns true while the wheel is still considered usable (transient failure),
// false once the disconnect threshold has been reached.
static bool _handleTxFailure(int idx, uint8_t serviceId, uint8_t paramId, const char* reason) {
    WheelConnState_t &w = _wheels[idx];
    const uint32_t now = millis();

    // Failures far apart should not accumulate into a disconnect decision.
    if (w.lastTxFailMs == 0 || (now - w.lastTxFailMs) > BLE_TX_FAIL_WINDOW_MS) {
        w.txFailStreak = 0;
    }
    w.lastTxFailMs = now;
    if (w.txFailStreak < 255) w.txFailStreak++;

    if ((debugFlags & DBG_BLE) &&
        (w.txFailStreak == 1 || (w.txFailStreak % BLE_TX_FAIL_LOG_EVERY) == 0)) {
        Serial.printf("[BLE] %s wheel TX fail: %s (svc=0x%02X param=0x%02X, streak=%u/%u)\n",
                      w.name ? w.name : "?", reason ? reason : "unknown",
                      serviceId, paramId,
                      (unsigned)w.txFailStreak,
                      (unsigned)BLE_TX_FAIL_DISCONNECT_STREAK);
    }

    if (w.txFailStreak < BLE_TX_FAIL_DISCONNECT_STREAK) {
        return true;   // Treat as transient: keep session alive.
    }

    Serial.printf("[BLE] %s wheel marked disconnected after TX failures (%u in %ums, last svc=0x%02X param=0x%02X, reason=%s)\n",
                  w.name ? w.name : "?",
                  (unsigned)w.txFailStreak,
                  (unsigned)BLE_TX_FAIL_WINDOW_MS,
                  serviceId, paramId,
                  reason ? reason : "unknown");
    w.connected     = false;
    w.protocolReady = false;
    w.driveModeBits = 0;
    return false;
}

static void _clearTxFailureState(WheelConnState_t& w) {
    if (w.txFailStreak > 0 && (debugFlags & DBG_BLE)) {
        Serial.printf("[BLE] %s wheel TX recovered after %u failures\n",
                      w.name ? w.name : "?",
                      (unsigned)w.txFailStreak);
    }
    w.txFailStreak = 0;
    w.lastTxFailMs = 0;
}

static void _txStatsCountAttempt(uint8_t paramId, const uint8_t* payload, uint8_t payloadLen) {
    portENTER_CRITICAL(&_txStatsMux);
    _txStats.totalAttempts++;
    switch (paramId) {
        case M25_PARAM_WRITE_REMOTE_SPEED:
            _txStats.remoteSpeed++;
            if (payload && payloadLen >= 2 && payload[0] == 0 && payload[1] == 0) {
                _txStats.remoteSpeedStop++;
            } else {
                _txStats.remoteSpeedMotion++;
            }
            break;
        case M25_PARAM_WRITE_DRIVE_MODE:
            _txStats.driveMode++;
            break;
        case M25_PARAM_WRITE_ASSIST_LEVEL:
            _txStats.assistLevel++;
            break;
        case M25_PARAM_READ_SOC:
            _txStats.readSoc++;
            break;
        case M25_PARAM_READ_SW_VERSION:
            _txStats.readFw++;
            break;
        case M25_PARAM_READ_CRUISE_VALUES:
            _txStats.readCruise++;
            break;
        default:
            _txStats.other++;
            break;
    }
    portEXIT_CRITICAL(&_txStatsMux);
}

static void _txStatsCountResult(bool ok) {
    portENTER_CRITICAL(&_txStatsMux);
    if (ok) {
        _txStats.totalSuccess++;
    } else {
        _txStats.totalFail++;
    }
    portEXIT_CRITICAL(&_txStatsMux);
}

bool _sendCommand(int idx, uint8_t serviceId, uint8_t paramId,
                  const uint8_t* payload, uint8_t payloadLen) {
    WheelConnState_t &w = _wheels[idx];
    _txStatsCountAttempt(paramId, payload, payloadLen);
#if M25_TRANSPORT_BLE
    // Quick pre-checks before taking the mutex to avoid unnecessary contention.
    // Use w.connected flag here (not isConnected()) ─ isConnected() acquires a
    // Bluedroid-internal lock and can block when the stack is in error recovery.
    if (!w.connected || w.rxChar == nullptr) return false;
#endif
#if M25_TRANSPORT_RFCOMM
    if (!w.connected || w.sppHandle == 0) return false;
#endif

    // Serialize all GATT writes across tasks (motor task Core 0, telemetry Core 1).
    if (_bleTxMutex && xSemaphoreTake(_bleTxMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return _handleTxFailure(idx, serviceId, paramId, "tx mutex timeout");
    }

    // Inside the lock: use w.connected flag only ─ NEVER call isConnected() here.
    // isConnected() acquires a Bluedroid-internal semaphore; if the BLE stack is
    // concurrently doing error recovery after rc=-1, that semaphore is held and
    // isConnected() blocks indefinitely, holding our mutex and deadlocking every
    // other caller.  The disconnect callback sets w.connected = false reliably.
    bool ok = false;
    if (w.connected
#if M25_TRANSPORT_BLE
        && w.rxChar
#endif
#if M25_TRANSPORT_RFCOMM
        && w.sppHandle != 0
#endif
        ) {
        uint8_t buf[128];
        uint8_t spp[32];
        uint8_t sppLen = 0;
        size_t len = _buildAndEncrypt(idx, serviceId, paramId, payload, payloadLen, buf, spp, &sppLen);
        if (len > 0) {
            if (debugFlags & DBG_PROTO) {
                _debugLogTxPacket(idx, serviceId, paramId, payload, payloadLen, spp, sppLen, buf, len);
            }
#if M25_TRANSPORT_BLE
            try {
                bool sent = w.rxChar->writeValue(buf, len, w.rxWriteWithResponse);
                if (!sent) {
                    // write-with-response: server rejected or timed out
                    ok = _handleTxFailure(idx, serviceId, paramId, "writeValue returned false");
                } else {
                    _clearTxFailureState(w);
                    ok = true;
                    _bleRecordFrame(BLE_REC_TX, (uint8_t)idx, buf, len);
                }
            } catch (...) {
                ok = _handleTxFailure(idx, serviceId, paramId, "writeValue exception");
            }
#endif
#if M25_TRANSPORT_RFCOMM
            esp_err_t rc = esp_spp_write(w.sppHandle, (int)len, buf);
            if (rc != ESP_OK) {
                char msg[64];
                snprintf(msg, sizeof(msg), "esp_spp_write rc=%s", esp_err_to_name(rc));
                ok = _handleTxFailure(idx, serviceId, paramId, msg);
            } else {
                _clearTxFailureState(w);
                ok = true;
                _bleRecordFrame(BLE_REC_TX, (uint8_t)idx, buf, len);
            }
#endif
        } else {
            ok = _handleTxFailure(idx, serviceId, paramId, "build/encrypt failed");
        }
    } else {
        ok = _handleTxFailure(idx, serviceId, paramId, "wheel not connected");
    }

    if (_bleTxMutex) xSemaphoreGive(_bleTxMutex);
    _txStatsCountResult(ok);
    return ok;
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
    
    // Parse based on service and parameter ID.
    // The wheel echoes the READ param ID back (not a separate STATUS ID),
    // so both IDs are accepted here.
    if (hdr->serviceId == M25_SRV_BATT_MGMT) {
        if ((hdr->paramId == M25_PARAM_STATUS_SOC ||
             hdr->paramId == M25_PARAM_READ_SOC) && len >= 1) {
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
            // Full cruise status (D2): 12-byte payload
            //   [0]   drive_mode
            //   [1]   push_rim
            //   [2-3] speed (uint16_be)
            //   [4]   soc
            //   [5-8] overall_distance (uint32_be, 0.01 m units)
            //   [9-10] push_counter (uint16_be)
            //   [11]  error
            data->cruiseValues.speed        = _parseUint16BE(p, 2);
            data->cruiseValues.distanceMm   = _parseUint32BE(p, 5);
            data->cruiseValues.distanceKm   = (float)data->cruiseValues.distanceMm * 0.00001f;
            data->cruiseValues.pushCounter  = _parseUint16BE(p, 9);
            return true;
        }
        else if (hdr->paramId == M25_PARAM_READ_CRUISE_VALUES && len >= 2) {
            // Compact read response (D1): 2-byte odometer, unit 0.01 m (same scale as D2)
            data->cruiseValues.distanceMm   = (uint32_t)_parseUint16BE(p, 0);
            data->cruiseValues.distanceKm   = (float)data->cruiseValues.distanceMm * 0.00001f;
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
            // 4-byte STATUS form: devState + major + minor + patch
            data->swVersion.devState = _parseUint8(p, 0);
            data->swVersion.major    = _parseUint8(p, 1);
            data->swVersion.minor    = _parseUint8(p, 2);
            data->swVersion.patch    = _parseUint8(p, 3);
            return true;
        }
        if (hdr->paramId == M25_PARAM_READ_SW_VERSION && len >= 3) {
            // 3-byte READ response: major + minor + patch (no devState prefix)
            data->swVersion.devState = 0;
            data->swVersion.major    = _parseUint8(p, 0);
            data->swVersion.minor    = _parseUint8(p, 1);
            data->swVersion.patch    = _parseUint8(p, 2);
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
            Serial.printf("[BLE] %s wheel ACK", wheelName);
            if (hdr->payloadLen > 0) {
                Serial.printf(" (payload %zu bytes:", hdr->payloadLen);
                for (size_t _pi = 0; _pi < hdr->payloadLen && _pi < 8; _pi++)
                    Serial.printf(" %02X", hdr->payload[_pi]);
                if (hdr->payloadLen > 8) Serial.print(" ...");
                Serial.print(")");
            }
            Serial.println();
        } else if (hdr->payloadLen > 0) {
            // ACK from a read command may carry the response data in its payload.
            // Log it unconditionally so protocol surprises are always visible.
            if (debugFlags & DBG_BLE) {
                Serial.printf("[BLE] %s wheel ACK with payload (srv=0x%02X, param=0x%02X, %zu bytes:",
                             wheelName, hdr->serviceId, hdr->paramId, hdr->payloadLen);
                for (size_t _pi = 0; _pi < hdr->payloadLen && _pi < 8; _pi++)
                    Serial.printf(" %02X", hdr->payload[_pi]);
                if (hdr->payloadLen > 8) Serial.print(" ...");
                Serial.println(")");
            }
        }
    }
    else {
        // Print parsed data based on response type (gate on DBG_TELEMETRY)
        if (debugFlags & DBG_TELEMETRY) {
            if (hdr->serviceId == M25_SRV_BATT_MGMT &&
                (hdr->paramId == M25_PARAM_STATUS_SOC || hdr->paramId == M25_PARAM_READ_SOC)) {
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
                else if (hdr->paramId == M25_PARAM_READ_CRUISE_VALUES) {
                    Serial.printf("[BLE] %s wheel odometer: %.3f km\n",
                                 wheelName, data->cruiseValues.distanceKm);
                }
            }
            else if (hdr->serviceId == M25_SRV_VERSION_MGMT &&
                     (hdr->paramId == M25_PARAM_STATUS_SW_VERSION ||
                      hdr->paramId == M25_PARAM_READ_SW_VERSION)) {
                Serial.printf("[BLE] %s wheel firmware: %d.%d.%d\n",
                             wheelName, data->swVersion.major, data->swVersion.minor,
                             data->swVersion.patch);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Telemetry cache update (called after each successfully parsed response)
// ---------------------------------------------------------------------------

void _updateWheelCache(int idx, const ResponseHeader* hdr, const ResponseData* data) {
    if (!hdr || !data || data->isNack || data->isAck) return;
    WheelConnState_t& w = _wheels[idx];

    if (hdr->serviceId == M25_SRV_BATT_MGMT &&
        (hdr->paramId == M25_PARAM_STATUS_SOC || hdr->paramId == M25_PARAM_READ_SOC)) {
        w.batteryPct   = (int8_t)data->soc.batteryPercent;
        w.batteryValid = true;
    }
    else if (hdr->serviceId == M25_SRV_VERSION_MGMT &&
             (hdr->paramId == M25_PARAM_STATUS_SW_VERSION ||
              hdr->paramId == M25_PARAM_READ_SW_VERSION)) {
        w.fwMajor = data->swVersion.major;
        w.fwMinor = data->swVersion.minor;
        w.fwPatch = data->swVersion.patch;
        w.fwValid = true;
    }
    else if (hdr->serviceId == M25_SRV_APP_MGMT &&
             (hdr->paramId == M25_PARAM_CRUISE_VALUES ||
              hdr->paramId == M25_PARAM_READ_CRUISE_VALUES)) {
        w.distanceKm    = data->cruiseValues.distanceKm;
        w.distanceValid = true;
    }
}

void _parseSppPacket(const uint8_t* spp, size_t sppLen, int wheelIdx) {
    const char* wheelName = (wheelIdx >= 0 && wheelIdx < WHEEL_COUNT &&
                             _wheels[wheelIdx].name)
                            ? _wheels[wheelIdx].name : "Unknown";
    ResponseHeader hdr;
    if (!_parseResponseHeader(spp, sppLen, &hdr)) {
        Serial.printf("[BLE] %s wheel: Failed to parse SPP header\n", wheelName);
        return;
    }

    ResponseData data;
    bool parsed = _parseResponseData(&hdr, &data);

    if (!parsed && debugFlags & DBG_BLE) {
        Serial.printf("[BLE] %s wheel: Unrecognized response (srv=0x%02X, param=0x%02X, payloadLen=%zu)\n",
                     wheelName, hdr.serviceId, hdr.paramId, hdr.payloadLen);
    }

    if (parsed) {
        _updateWheelCache(wheelIdx, &hdr, &data);
    }

    _printResponse(wheelName, &hdr, parsed ? &data : nullptr);
}

// ---------------------------------------------------------------------------
// BLE notification callback
// ---------------------------------------------------------------------------

#if M25_TRANSPORT_BLE
void _notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    // Find which wheel this notification is for
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheels[i].txChar == pChar) {
            WheelConnState_t &w = _wheels[i];
            
            // Capture raw incoming frame for traffic recorder
            _bleRecordFrame(BLE_REC_RX, (uint8_t)i, pData, length);
            // Update notify timestamp so the stale-notify watchdog knows this wheel is alive
            _wheels[i].lastNotifyMs = millis();

            // Raw hex dump (DBG_PROTO: low-level frame debugging)
            if (debugFlags & DBG_PROTO) {
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
            
            if (debugFlags & DBG_PROTO) {
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
                _parseSppPacket(sppPacket, sppLen, i);
            } else {
                Serial.printf("[BLE] %s wheel: Decryption failed\n", w.name ? w.name : "Unknown");
            }
            
            break;
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Disconnect callback implementation
// ---------------------------------------------------------------------------

#if M25_TRANSPORT_BLE
void M25DisconnectCallback::onConnect(BLEClient*) {}

void M25DisconnectCallback::onDisconnect(BLEClient*) {
    _wheels[wheelIdx].connected     = false;
    _wheels[wheelIdx].protocolReady = false;
    _wheels[wheelIdx].driveModeBits = 0;
    _wheels[wheelIdx].txFailStreak  = 0;
    _wheels[wheelIdx].lastTxFailMs  = 0;
    
    // Only print message for active wheels (respect WHEEL_MODE)
    if (_wheelActive(wheelIdx)) {
        Serial.printf("[BLE] %s wheel disconnected\n", _wheels[wheelIdx].name);
    }
}
#endif

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

#if M25_TRANSPORT_RFCOMM
    if (strlen(w.mac) != 17 || !_parseMacToBda(w.mac, w.bda)) {
        Serial.printf("[RFCOMM] %s wheel: Invalid MAC '%s'\n", wheelName, w.mac);
        w.consecutiveFails++;
        return false;
    }
    if (!_rfcommReady) {
        Serial.printf("[RFCOMM] %s wheel: SPP stack not ready yet\n", wheelName);
        w.consecutiveFails++;
        return false;
    }

    _rfcommOpenEvt[idx] = false;
    _rfcommCloseEvt[idx] = false;
    _rfcommOpenStatus[idx] = -1;
    _rfcommPendingIdx = idx;
    _rfRxLen[idx] = 0;
    w.sppHandle = 0;

    Serial.printf("[RFCOMM] Connecting %s wheel over SPP channel %d...\n",
                  wheelName, RFCOMM_CHANNEL);
    esp_err_t rc = esp_spp_connect(ESP_SPP_SEC_NONE,
                                   ESP_SPP_ROLE_MASTER,
                                   RFCOMM_CHANNEL,
                                   w.bda);
    if (rc != ESP_OK) {
        Serial.printf("[RFCOMM] %s wheel: esp_spp_connect failed: %s\n",
                      wheelName, esp_err_to_name(rc));
        w.consecutiveFails++;
        return false;
    }

    const uint32_t startMs = millis();
    while (!_rfcommOpenEvt[idx] && (millis() - startMs) < RFCOMM_CONNECT_TIMEOUT_MS) {
        extern void ledTick();
        extern void buzzerTick();
        ledTick();
        buzzerTick();
        delay(10);
    }
    if (!_rfcommOpenEvt[idx] || _rfcommOpenStatus[idx] != ESP_SPP_SUCCESS || w.sppHandle == 0) {
        Serial.printf("[RFCOMM] %s wheel: open timeout/status=%d handle=%u\n",
                      wheelName,
                      _rfcommOpenStatus[idx],
                      (unsigned)w.sppHandle);
        if (w.sppHandle != 0) {
            esp_spp_disconnect(w.sppHandle);
            w.sppHandle = 0;
        }
        w.connected = false;
        w.consecutiveFails++;
        return false;
    }

    w.connected = true;
    w.txFailStreak = 0;
    w.lastTxFailMs = 0;
#endif

#if M25_TRANSPORT_BLE
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
        // Library logs status code above; "Unknown ESP_ERR" means GATT status (not in esp_err_to_name).
        // status=133 = ESP_GATT_ERROR (not connectable / busy); see _bleErrStr() for others.
        Serial.printf("[BLE] %s wheel: GATT connect FAILED\n", wheelName);
        w.consecutiveFails++;
        return false;
    }

    // Probe known service+characteristic profiles in order.
    // tx = notify characteristic, rx = write characteristic.
    struct _UUIDProfile {
        const char* name;
        const char* service;
        const char* tx;
        const char* rx;
    };
    static const _UUIDProfile candidates[] = {
        // Real M25 V2 wheels (ISSC Transparent UART style service)
        { "M25V2", "49535343-fe7d-4ae5-8fa9-9fafd205e455", "49535343-1e4d-4bd9-ba61-23c647249616", "49535343-8841-43f4-a8d4-ecbe34729bb3" },
        // Real M25 V1 wheels (single char handles write + notify)
        { "M25V1", "c9e61e27-93c0-45c0-b5f9-702e971daa2e", "49535343-026e-3a9b-954c-97daef17e26e", "49535343-026e-3a9b-954c-97daef17e26e" },
        // Fake wheels used in this repo
        { "FAKE_LEFT", "00001101-0000-1000-8000-00805F9B34FB", "00001101-0000-1000-8000-00805F9B34FB", "00001102-0000-1000-8000-00805F9B34FB" },
        { "FAKE_RIGHT", "00001101-0000-1000-8000-00805F9B34FB", "00001103-0000-1000-8000-00805F9B34FB", "00001104-0000-1000-8000-00805F9B34FB" },
    };

    BLERemoteService* svc = nullptr;
    BLERemoteCharacteristic* rxChar = nullptr;
    BLERemoteCharacteristic* txChar = nullptr;

    for (int _gattRetry = 0;
         _gattRetry < BLE_SERVICE_DISCOVERY_RETRIES && (!svc || !rxChar);
         _gattRetry++) {
        for (const auto& c : candidates) {
            svc = w.client->getService(BLEUUID(c.service));
            if (!svc) {
                continue;
            }

            rxChar = svc->getCharacteristic(BLEUUID(c.rx));
            if (!rxChar) {
                continue;
            }

            txChar = svc->getCharacteristic(BLEUUID(c.tx));
            Serial.printf("[BLE] %s wheel: matched %s profile (service=%s, rx=%s)\n",
                          wheelName, c.name, c.service, c.rx);
            break;
        }

        if (!rxChar && _gattRetry < (BLE_SERVICE_DISCOVERY_RETRIES - 1)) {
            Serial.printf("[BLE] %s wheel: known services not ready, retrying (%d/%d)...\n",
                          wheelName,
                          _gattRetry + 1,
                          BLE_SERVICE_DISCOVERY_RETRIES);
            delay(BLE_SERVICE_DISCOVERY_DELAY_MS);
        }
    }

    if (!rxChar) {
        // Fallback: scan all discovered services for any write+notify chars.
        // This keeps compatibility with unknown variants without hardcoded UUIDs.
        std::map<std::string, BLERemoteService*>* allServices = w.client->getServices();
        if (allServices) {
            for (auto const& svcEntry : *allServices) {
                BLERemoteService* s = svcEntry.second;
                if (!s) continue;

                BLERemoteCharacteristic* anyNotify = nullptr;
                BLERemoteCharacteristic* anyWrite = nullptr;
                std::map<std::string, BLERemoteCharacteristic*>* chars = s->getCharacteristics();
                if (!chars) continue;

                for (auto const& chEntry : *chars) {
                    BLERemoteCharacteristic* ch = chEntry.second;
                    if (!ch) continue;
                    if (!anyNotify && ch->canNotify()) anyNotify = ch;
                    if (!anyWrite && (ch->canWrite() || ch->canWriteNoResponse())) anyWrite = ch;
                    if (anyNotify && anyWrite) break;
                }

                if (anyWrite) {
                    rxChar = anyWrite;
                    txChar = anyNotify ? anyNotify : anyWrite;
                    svc = s;
                    Serial.printf("[BLE] %s wheel: fallback matched service %s\n",
                                  wheelName,
                                  svcEntry.first.c_str());
                    break;
                }
            }
        }
    }

    if (!rxChar) {
        Serial.printf("[BLE] %s wheel: no usable write/notify characteristics found\n", wheelName);
        w.client->disconnect();
        w.consecutiveFails++;
        return false;
    }
    w.rxChar = rxChar;
    w.txChar = txChar;

    // Detect write mode required by the wheel's RX characteristic.
    {
        // WRITE_NR = write without response; WRITE = write with response.
        // Sending without response to a WRITE-only char is silently dropped by ATT.
        bool hasWriteNR = rxChar->canWriteNoResponse();
        bool hasWrite   = rxChar->canWrite();
        w.rxWriteWithResponse = hasWrite && !hasWriteNR;
        Serial.printf("[BLE] %s wheel: RX properties canWrite=%d canWriteNR=%d -> %s\n",
                      wheelName, hasWrite, hasWriteNR,
                      w.rxWriteWithResponse ? "write-with-response" : "write-without-response");
    }

    if (w.txChar && w.txChar->canNotify()) {
        // ESP32 BLE stack needs time after getCharacteristic() before descriptor retrieval
        uint32_t preNotifyDelay = (idx > 0) ? 800 : 500;
        delay(preNotifyDelay);
        
        Serial.printf("[BLE] %s wheel: registering notifications (pass 1)...\n", wheelName);
        w.txChar->registerForNotify(_notifyCallback);
        Serial.printf("[BLE] %s wheel: waiting %d ms for stability...\n",
                      wheelName, BLE_NOTIFY_RETRY_DELAY_MS);
        delay(BLE_NOTIFY_RETRY_DELAY_MS);
        Serial.printf("[BLE] %s wheel: registering notifications (pass 2)...\n", wheelName);
        w.txChar->registerForNotify(_notifyCallback);
        Serial.printf("[BLE] %s wheel: Notifications enabled\n", wheelName);
    }
    w.receivedFirstAck = false;  // Reset ACK flag

    w.connected = true;
    w.txFailStreak = 0;
    w.lastTxFailMs = 0;
#endif

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
#if M25_TRANSPORT_BLE
        w.client->disconnect();
#endif
#if M25_TRANSPORT_RFCOMM
        if (w.sppHandle) esp_spp_disconnect(w.sppHandle);
        w.sppHandle = 0;
#endif
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
    w.lastNotifyMs  = millis();  // seed timer; watchdog won't trip before first real notify arrives
    w.consecutiveFails = 0;
    Serial.printf("[BLE] %s wheel ready\n", wheelName);
    return true;
}

// ---------------------------------------------------------------------------
// Public API implementations
// ---------------------------------------------------------------------------

void bleInit(const char* deviceName) {
    // Create the GATT-write mutex before any possible _sendCommand() call.
    if (!_bleTxMutex) {
        _bleTxMutex = xSemaphoreCreateMutex();
        if (!_bleTxMutex) Serial.println("[BLE] ERROR: failed to create TX mutex");
    }

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
            Serial.printf("[BLE] _wheels[%d]: name=%s, mac=%s",
                          i, _wheels[i].name ? _wheels[i].name : "NULL",
                          _wheels[i].mac);
#if M25_TRANSPORT_BLE
            Serial.printf(", client=%p", (void*)_wheels[i].client);
#endif
            Serial.println();
        }
    }
#if M25_TRANSPORT_BLE
    BLEDevice::init(deviceName);
    Serial.println("[BLE] Device initialized");
#endif
#if M25_TRANSPORT_RFCOMM
    if (!_rfcommInitDone) {
        _rfcommReady = false;

        esp_err_t rc = ESP_OK;
        esp_bt_controller_status_t ctl = esp_bt_controller_get_status();
        if (ctl == ESP_BT_CONTROLLER_STATUS_IDLE) {
            esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            rc = esp_bt_controller_init(&bt_cfg);
            if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                Serial.printf("[RFCOMM] controller init failed: %s\n", esp_err_to_name(rc));
            }
            ctl = esp_bt_controller_get_status();
        }

        if (ctl != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            // Arduino/IDF configs often expect BTDM mode, not classic-only mode.
            rc = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
            if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                Serial.printf("[RFCOMM] controller enable failed: %s\n", esp_err_to_name(rc));
            }
        }

        esp_bluedroid_status_t bl = esp_bluedroid_get_status();
        if (bl == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            rc = esp_bluedroid_init();
            if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                Serial.printf("[RFCOMM] bluedroid init failed: %s\n", esp_err_to_name(rc));
            }
            bl = esp_bluedroid_get_status();
        }

        if (bl != ESP_BLUEDROID_STATUS_ENABLED) {
            rc = esp_bluedroid_enable();
            if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                Serial.printf("[RFCOMM] bluedroid enable failed: %s\n", esp_err_to_name(rc));
            }
        }

        if (!_rfcommGapCbRegistered) {
            rc = esp_bt_gap_register_callback(_rfcommGapCb);
            if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
                _rfcommGapCbRegistered = true;
            } else {
                Serial.printf("[RFCOMM] gap callback register failed: %s\n", esp_err_to_name(rc));
            }
        }

        rc = esp_bt_gap_set_device_name(deviceName);
        if (rc != ESP_OK) {
            Serial.printf("[RFCOMM] set device name failed: %s\n", esp_err_to_name(rc));
        }

        if (!_rfcommCbRegistered) {
            rc = esp_spp_register_callback(_rfcommSppCb);
            if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
                _rfcommCbRegistered = true;
            } else {
                Serial.printf("[RFCOMM] spp callback register failed: %s\n", esp_err_to_name(rc));
            }
        }

        if (!_rfcommSppInitRequested) {
            esp_spp_cfg_t sppCfg = BT_SPP_DEFAULT_CONFIG();
            sppCfg.mode = ESP_SPP_MODE_CB;
            rc = esp_spp_enhanced_init(&sppCfg);
            if (rc == ESP_OK) {
                _rfcommSppInitRequested = true;
            } else if (rc == ESP_ERR_INVALID_STATE) {
                // Already initialized from an earlier run; proceed.
                _rfcommSppInitRequested = true;
                _rfcommReady = true;
            } else {
                Serial.printf("[RFCOMM] spp init failed: %s\n", esp_err_to_name(rc));
            }
        }

        uint32_t sppWaitStart = millis();
        while (!_rfcommReady && (millis() - sppWaitStart) < 3000) {
            delay(10);
        }
        if (!_rfcommReady) {
            Serial.println("[RFCOMM] WARNING: SPP init event timeout");
        }
        _rfcommInitDone = true;
    }
    Serial.println("[RFCOMM] SPP client initialized");
#endif
    Serial.printf("[BLE] Wheel mode: %s\n", WHEEL_MODE_NAME);
    
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
#if M25_TRANSPORT_BLE
        _callbacks[i].wheelIdx = (uint8_t)i;
#endif
    }
}

void bleResetWheel(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return;
    WheelConnState_t &w = _wheels[idx];
    const char* name = w.name ? w.name : "?";

    Serial.printf("[BLE] Hard reset: %s wheel\n", name);

    // Disconnect the underlying GATT connection if still open, but intentionally
    // reuse the existing BLEClient object on the next attempt.
    // Nulling w.client would force BLEDevice::createClient() on every retry,
    // which leaks GATT client slots from the ESP32 BLE stack and eventually
    // corrupts its internal state, causing persistent encryption failures.
    if (
#if M25_TRANSPORT_BLE
        w.client && w.client->isConnected()
#else
        w.sppHandle != 0
#endif
    ) {
#if M25_TRANSPORT_BLE
        w.client->disconnect();
#endif
#if M25_TRANSPORT_RFCOMM
        esp_spp_disconnect(w.sppHandle);
#endif
    }

    // Clear all protocol state; preserve mac, name, key, and client object
    w.connected        = false;
    w.protocolReady    = false;
    w.telegramId       = M25_TELEGRAM_ID_START;
    w.driveModeBits    = 0;
#if M25_TRANSPORT_BLE
    w.rxChar               = nullptr;
    w.txChar               = nullptr;
    w.rxWriteWithResponse  = false;
#endif
#if M25_TRANSPORT_RFCOMM
    w.sppHandle            = 0;
    _rfRxLen[idx]          = 0;
#endif
    w.receivedFirstAck     = false;
    w.consecutiveFails = 0;
    w.lastNotifyMs     = 0;
    w.txFailStreak     = 0;
    w.lastTxFailMs     = 0;

    // Invalidate telemetry cache so stale data is not served after reconnect
    w.batteryValid  = false;
    w.fwValid       = false;
    w.distanceValid = false;
}

void bleFullReset() {
    Serial.println("[BLE] Full stack reset: deinit + reinit");
    // Hard-reset all GATT clients before tearing down the stack
    for (int i = 0; i < WHEEL_COUNT; i++) bleResetWheel(i);
    // Let Bluedroid finish teardown before deinit
    delay(200);
#if M25_TRANSPORT_BLE
    BLEDevice::deinit(true);
#endif
#if M25_TRANSPORT_RFCOMM
    esp_spp_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    _rfcommInitDone = false;
    _rfcommReady = false;
    _rfcommCbRegistered = false;
    _rfcommGapCbRegistered = false;
    _rfcommSppInitRequested = false;
#endif
    delay(500);
    // Reinit restores compile-time MAC/key defaults from device_config.h.
    // Runtime bleSetMac/bleSetKey overrides are NOT preserved across a full reset.
    bleInit();
    Serial.println("[BLE] Stack reset complete");
}

bool bleConnectWheel(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return false;
    if (!_wheelActive(idx)) return true;   // Inactive wheel is not a failure
    if (_wheels[idx].connected)  return true;  // Already connected
    return _connectWheel(idx);
}

void bleConnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && !_wheels[i].connected) {
            if (debugFlags & DBG_BLE) {
                Serial.printf("[BLE] bleConnect: About to connect wheel %d, MAC='%s' (len=%d)\n",
                              i, _wheels[i].mac, (int)strlen(_wheels[i].mac));
            }
            
            bool success = _connectWheel(i);
            
            // Delay between wheels for BLE stack to settle (dual-wheel mode)
            if (success && i < WHEEL_COUNT - 1 && _wheelActive(i + 1)) {
                delay(BLE_INTER_WHEEL_DELAY_MS);
            }
        }
    }
}

void bleDisconnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        WheelConnState_t &w = _wheels[i];
        if (w.connected) {
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
        #if M25_TRANSPORT_BLE
        if (w.client && w.client->isConnected()) w.client->disconnect();
        #endif
        #if M25_TRANSPORT_RFCOMM
            if (w.sppHandle) esp_spp_disconnect(w.sppHandle);
            w.sppHandle = 0;
        #endif
        w.connected     = false;
        w.protocolReady = false;
        w.driveModeBits = 0;
        w.txFailStreak  = 0;
        w.lastTxFailMs  = 0;
    }
}

bool bleIsConnected(int wheelIdx) {
    WheelConnState_t &w = _wheels[wheelIdx];
#if M25_TRANSPORT_BLE
    return w.connected && w.protocolReady
        && w.client != nullptr && w.client->isConnected();
#endif
#if M25_TRANSPORT_RFCOMM
    return w.connected && w.protocolReady && w.sppHandle != 0;
#endif
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

// ---------------------------------------------------------------------------
// Motor write task - runs on Core 0 alongside the BLE stack.
// Prevents writeValue() blocking from freezing loop() on Core 1.
// Queue depth 1 with overwrite semantics: only the latest command is kept.
// ---------------------------------------------------------------------------

struct _MotorCmd {
    bool    isStop;
    float   left;      // percent, signed
    float   right;
};

static QueueHandle_t  _motorQueue   = nullptr;
static volatile bool  _motorWriteOk = true;   // cleared by task on write failure

static bool _writeDriveModeIfNeeded(int idx, uint8_t targetMode) {
    if (idx < 0 || idx >= WHEEL_COUNT) return false;
    if (!bleIsConnected(idx)) return false;

    WheelConnState_t &w = _wheels[idx];
    if (w.driveModeBits == targetMode) return true;

    bool sent = _sendCommand(idx, M25_SRV_APP_MGMT, M25_PARAM_WRITE_DRIVE_MODE, &targetMode, 1);
    if (sent) {
        w.driveModeBits = targetMode;
    }
    return sent;
}

static void _bleMotorTask(void* /*pv*/) {
    _MotorCmd cmd;
    uint32_t motorFailStreak = 0;  // consecutive failed write cycles (any wheel)
    uint32_t stopLogCounter[WHEEL_COUNT] = {0, 0};
    for (;;) {
        if (xQueueReceive(_motorQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        bool ok = true;
        for (int i = 0; i < WHEEL_COUNT; i++) {
            if (!_wheelActive(i)) continue;

            if (debugFlags & DBG_MOTOR) {
                const char* wn = _wheels[i].name ? _wheels[i].name : "?";
                if (!bleIsConnected(i)) {
                    Serial.printf("[Motor] -> %s (not connected)\n", wn);
                } else if (cmd.isStop) {
                    if (_motorStopLogEnabled) {
                        stopLogCounter[i]++;
                        uint16_t every = _motorStopLogEvery;
                        if (every == 0) every = 1;
                        if ((stopLogCounter[i] % every) == 0) {
                            Serial.printf("[Motor] -> %s STOP (%lu)%s\n",
                                          wn,
                                          (unsigned long)stopLogCounter[i],
                                          (every > 1) ? " [throttled]" : "");
                        }
                    }
                } else {
                    float pct = (i == WHEEL_LEFT) ? -cmd.left : cmd.right;
                    Serial.printf("[Motor] -> %s %.0f%%\n", wn, (double)pct);
                }
            }

            if (!bleIsConnected(i)) continue;

            WheelConnState_t &w = _wheels[i];
            uint8_t targetDriveMode = cmd.isStop
                ? (uint8_t)((w.driveModeBits | M25_DRIVE_MODE_REMOTE) & (uint8_t)~M25_DRIVE_MODE_CRUISE)
                : (uint8_t)(w.driveModeBits | M25_DRIVE_MODE_REMOTE | M25_DRIVE_MODE_CRUISE);

            if (!cmd.isStop) {
                bool modeOk = _writeDriveModeIfNeeded(i, targetDriveMode);
                if (!modeOk && (debugFlags & DBG_BLE)) {
                    Serial.printf("[BLE] drive mode update failed on wheel %d\n", i);
                }
                ok &= modeOk;
                if (!modeOk) continue;
            }

            uint8_t spd[2];
            if (cmd.isStop) {
                spd[0] = spd[1] = 0;
            } else {
                float   pct = (i == WHEEL_LEFT) ? -cmd.left : cmd.right;
                int16_t raw = (int16_t)constrain(pct * M25_SPEED_SCALE, -32768.0f, 32767.0f);
                spd[0] = (uint8_t)((raw >> 8) & 0xFF);
                spd[1] = (uint8_t)(raw        & 0xFF);
            }

            bool sent = _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
            if (!sent && (debugFlags & DBG_BLE)) {
                Serial.printf("[BLE] motor write failed on wheel %d\n", i);
            }
            ok &= sent;

            if (cmd.isStop && sent) {
                bool modeOk = _writeDriveModeIfNeeded(i, targetDriveMode);
                if (!modeOk && (debugFlags & DBG_BLE)) {
                    Serial.printf("[BLE] stop drive mode restore failed on wheel %d\n", i);
                }
                ok &= modeOk;
            }
        }
        // Log write failures unconditionally (not behind DBG_BLE):
        // first failure is always printed; then every 20 cycles (~1 s at 20 Hz).
        if (!ok) {
            motorFailStreak++;
            if (motorFailStreak == 1 || (motorFailStreak % 20) == 0) {
                Serial.printf("[Motor] write FAILED (streak: %u cycles @ 20 Hz)\n",
                              (unsigned)motorFailStreak);
            }
        } else if (motorFailStreak > 0) {
            Serial.printf("[Motor] write recovered after %u failed cycles\n",
                          (unsigned)motorFailStreak);
            motorFailStreak = 0;
        }
        _motorWriteOk = ok;
        // Pace writes to ~20 Hz and yield to IDLE0 on Core 0. Without this,
        // xQueueOverwrite at loop() rate keeps the queue permanently occupied,
        // xQueueReceive never blocks, and the motor task starves IDLE0 until
        // the task watchdog fires.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void bleStartMotorTask() {
    // Queue of 1: xQueueOverwrite always keeps the newest command
    _motorQueue = xQueueCreate(1, sizeof(_MotorCmd));
    if (!_motorQueue) {
        Serial.println("[BLE] ERROR: failed to create motor queue");
        return;
    }
    // Pin to Core 0 where the BLE stack lives; priority 5 matches BLE event task
    xTaskCreatePinnedToCore(_bleMotorTask, "ble_motor", 4096, nullptr, 5, nullptr, 0);
    Serial.println("[BLE] Motor write task started on Core 0");
}

bool bleLastMotorWriteOk() {
    return _motorWriteOk;
}

void bleResetMotorWriteOk() {
    _motorWriteOk = true;
}

uint32_t bleGetLastNotifyMs(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return 0;
    return _wheels[idx].lastNotifyMs;
}

void bleResetNotifyTimers() {
    uint32_t now = millis();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheelActive(i) && _wheels[i].connected) {
            _wheels[i].lastNotifyMs = now;
        }
    }
}

bool bleSendStop() {
    if (_motorQueue) {
        _MotorCmd c = { true, 0.0f, 0.0f };
        xQueueOverwrite(_motorQueue, &c);
        return true;
    }
    // Fallback (task not started yet - e.g. during protocol init)
    uint8_t spd[2] = { 0, 0 };
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!bleIsConnected(i)) continue;
        ok &= _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
        uint8_t targetDriveMode = (uint8_t)((_wheels[i].driveModeBits | M25_DRIVE_MODE_REMOTE) &
                                            (uint8_t)~M25_DRIVE_MODE_CRUISE);
        ok &= _writeDriveModeIfNeeded(i, targetDriveMode);
    }
    return ok;
}

bool bleSendMotorCommand(float leftPercent, float rightPercent) {
    if (_motorQueue) {
        _MotorCmd c = { false, leftPercent, rightPercent };
        xQueueOverwrite(_motorQueue, &c);
        return true;
    }
    // Fallback
    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!bleIsConnected(i)) continue;
        uint8_t targetDriveMode = (uint8_t)(_wheels[i].driveModeBits |
                                            M25_DRIVE_MODE_REMOTE |
                                            M25_DRIVE_MODE_CRUISE);
        bool modeOk = _writeDriveModeIfNeeded(i, targetDriveMode);
        ok &= modeOk;
        if (!modeOk) continue;
        float   pct = (i == WHEEL_LEFT) ? -leftPercent : rightPercent;
        int16_t raw = (int16_t)constrain(pct * M25_SPEED_SCALE, -32768.0f, 32767.0f);
        uint8_t spd[2] = { (uint8_t)((raw >> 8) & 0xFF), (uint8_t)(raw & 0xFF) };
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
    // Auto-stop record when its duration expires, then dump the log
    if (_recordActive && millis() >= _recordEndMs) {
        _recordActive = false;
        Serial.printf("[Record] Auto-stopped - %d entries captured\n", (int)_recordCount);
        bleRecordDump();
    }

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

void bleSetMotorStopLogEnabled(bool enable) {
    _motorStopLogEnabled = enable;
}

bool bleGetMotorStopLogEnabled() {
    return _motorStopLogEnabled;
}

void bleSetMotorStopLogEvery(uint16_t every) {
    _motorStopLogEvery = (every == 0) ? 1 : every;
}

uint16_t bleGetMotorStopLogEvery() {
    return _motorStopLogEvery;
}

void blePrintTxStats() {
    TxStats s;
    portENTER_CRITICAL(&_txStatsMux);
    s = _txStats;
    portEXIT_CRITICAL(&_txStatsMux);

    Serial.println("[TX] --- Command TX Stats ---");
    Serial.printf("[TX] total attempts=%lu  success=%lu  fail=%lu\n",
                  (unsigned long)s.totalAttempts,
                  (unsigned long)s.totalSuccess,
                  (unsigned long)s.totalFail);
    Serial.printf("[TX] WRITE_REMOTE_SPEED=%lu  stop=%lu  motion=%lu\n",
                  (unsigned long)s.remoteSpeed,
                  (unsigned long)s.remoteSpeedStop,
                  (unsigned long)s.remoteSpeedMotion);
    Serial.printf("[TX] WRITE_DRIVE_MODE=%lu  WRITE_ASSIST_LEVEL=%lu\n",
                  (unsigned long)s.driveMode,
                  (unsigned long)s.assistLevel);
    Serial.printf("[TX] READ_SOC=%lu  READ_SW_VERSION=%lu  READ_CRUISE_VALUES=%lu  other=%lu\n",
                  (unsigned long)s.readSoc,
                  (unsigned long)s.readFw,
                  (unsigned long)s.readCruise,
                  (unsigned long)s.other);
}

void bleResetTxStats() {
    portENTER_CRITICAL(&_txStatsMux);
    memset(&_txStats, 0, sizeof(_txStats));
    portEXIT_CRITICAL(&_txStatsMux);
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
#if M25_TRANSPORT_BLE
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
#endif
#if M25_TRANSPORT_RFCOMM
    if (w.sppHandle) {
        if (debugFlags & DBG_BLE) Serial.println("[RFCOMM] Disconnecting existing SPP handle...");
        esp_spp_disconnect(w.sppHandle);
        w.sppHandle = 0;
    }
#endif
    if (debugFlags & DBG_BLE) {
        Serial.println("[BLE] Updating wheel state...");
    }
    w.connected     = false;
    w.protocolReady = false;
    w.consecutiveFails = 0;
    w.txFailStreak  = 0;
    w.lastTxFailMs  = 0;
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
    w.txFailStreak = 0;
    w.lastTxFailMs = 0;
    
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

// ---------------------------------------------------------------------------
// Telemetry request functions
// ---------------------------------------------------------------------------

static bool _requestOne(int idx, uint8_t svc, uint8_t param) {
    if (idx < 0 || idx >= WHEEL_COUNT || !bleIsConnected(idx)) return false;
    return _sendCommand(idx, svc, param, nullptr, 0);
}

static bool _requestAll(uint8_t svc, uint8_t param) {
    bool ok = false;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok |= _sendCommand(i, svc, param, nullptr, 0);
        }
    }
    return ok;
}

bool bleRequestSOC(int idx) {
    if (idx < 0) return _requestAll(M25_SRV_BATT_MGMT, M25_PARAM_READ_SOC);
    return _requestOne(idx, M25_SRV_BATT_MGMT, M25_PARAM_READ_SOC);
}

bool bleRequestFirmwareVersion(int idx) {
    if (idx < 0) return _requestAll(M25_SRV_VERSION_MGMT, M25_PARAM_READ_SW_VERSION);
    return _requestOne(idx, M25_SRV_VERSION_MGMT, M25_PARAM_READ_SW_VERSION);
}

bool bleRequestCruiseValues(int idx) {
    if (idx < 0) return _requestAll(M25_SRV_APP_MGMT, M25_PARAM_READ_CRUISE_VALUES);
    return _requestOne(idx, M25_SRV_APP_MGMT, M25_PARAM_READ_CRUISE_VALUES);
}

// ---------------------------------------------------------------------------
// Cached telemetry getters
// ---------------------------------------------------------------------------

int8_t bleGetBattery(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT) return -1;
    if (!_wheels[idx].batteryValid) return -1;
    return _wheels[idx].batteryPct;
}

bool bleGetFirmwareVersion(int idx, uint8_t& major, uint8_t& minor, uint8_t& patch) {
    if (idx < 0 || idx >= WHEEL_COUNT || !_wheels[idx].fwValid) return false;
    major = _wheels[idx].fwMajor;
    minor = _wheels[idx].fwMinor;
    patch = _wheels[idx].fwPatch;
    return true;
}

float bleGetDistanceKm(int idx) {
    if (idx < 0 || idx >= WHEEL_COUNT || !_wheels[idx].distanceValid) return -1.0f;
    return _wheels[idx].distanceKm;
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
        Serial.printf("  txFailStreak : %u\n", (unsigned)w.txFailStreak);
        Serial.printf("  driveMode    : 0x%02X\n", w.driveModeBits);
        Serial.printf("  telegramId   : %u\n",     (unsigned)w.telegramId);
        // Cached telemetry
        if (w.batteryValid) {
            Serial.printf("  battery      : %d%%\n", (int)w.batteryPct);
        } else {
            Serial.printf("  battery      : (not yet received)\n");
        }
        if (w.fwValid) {
            Serial.printf("  firmware     : %d.%d.%d\n", w.fwMajor, w.fwMinor, w.fwPatch);
        } else {
            Serial.printf("  firmware     : (not yet received)\n");
        }
        if (w.distanceValid) {
            Serial.printf("  distance     : %.3f km\n", w.distanceKm);
        } else {
            Serial.printf("  distance     : (not yet received)\n");
        }
    }
    Serial.printf("[BLE] autoReconnect: %s\n", _bleAutoReconnect ? "ON" : "off");
}
