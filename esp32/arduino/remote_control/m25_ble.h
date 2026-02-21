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
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <mbedtls/aes.h>
#include <esp_system.h>     // esp_fill_random()
#include "device_config.h"

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
#define BLE_RECONNECT_DELAY_MS  5000

// ---------------------------------------------------------------------------
// Wheel slot indices
// ---------------------------------------------------------------------------
#define WHEEL_LEFT  0
#define WHEEL_RIGHT 1
#define WHEEL_COUNT 2

// ---------------------------------------------------------------------------
// CRC-16 lookup table (m25_protocol.py CRC_TABLE, init value 0xFFFF)
// ---------------------------------------------------------------------------
static const uint16_t _crcTable[256] = {
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
        crc = (crc >> 8) ^ _crcTable[(crc ^ data[i]) & 0xFF];
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
    const char*                  mac;
    const char*                  name;
    const uint8_t*               key;
    bool                         connected;
    bool                         protocolReady;      // SYSTEM_MODE + DRIVE_MODE acked
    uint8_t                      telegramId;         // SPP sequence counter
    uint8_t                      driveModeBits;      // current DRIVE_MODE byte
    BLEClient*                   client;
    BLERemoteCharacteristic*     rxChar;
    uint32_t                     lastConnectAttemptMs;
};

// ---------------------------------------------------------------------------
// Encryption keys (from device_config.h)
// ---------------------------------------------------------------------------
static const uint8_t _keyLeft[16]  = ENCRYPTION_KEY_LEFT;
static const uint8_t _keyRight[16] = ENCRYPTION_KEY_RIGHT;

// ---------------------------------------------------------------------------
// Global wheel state
// ---------------------------------------------------------------------------
static WheelConnState_t _wheels[WHEEL_COUNT] = {
    { LEFT_WHEEL_MAC,  "Left",  _keyLeft,  false, false, M25_TELEGRAM_ID_START, 0, nullptr, nullptr, 0 },
    { RIGHT_WHEEL_MAC, "Right", _keyRight, false, false, M25_TELEGRAM_ID_START, 0, nullptr, nullptr, 0 },
};

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
    // 1. PKCS7 pad SPP to 16-byte boundary
    uint8_t padLen    = (uint8_t)(16 - (sppLen % 16));
    uint8_t paddedLen = sppLen + padLen;   // always 16 or 32 for our packets

    uint8_t padded[32];
    memcpy(padded, spp, sppLen);
    memset(padded + sppLen, padLen, padLen);

    // 2. Random IV
    uint8_t iv[16];
    esp_fill_random(iv, sizeof(iv));

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
    if (!w.connected || w.rxChar == nullptr) return false;
    uint8_t buf[128];
    size_t len = _buildAndEncrypt(idx, serviceId, paramId, payload, payloadLen, buf);
    if (len == 0) return false;
    w.rxChar->writeValue(buf, len, false);
    return true;
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
        Serial.printf("[BLE] %s wheel disconnected\n", _wheels[wheelIdx].name);
    }
};
static M25DisconnectCallback _callbacks[WHEEL_COUNT];

// ---------------------------------------------------------------------------
// Initialization - call once in setup()
// ---------------------------------------------------------------------------
inline void bleInit(const char* deviceName = "M25-Remote") {
    BLEDevice::init(deviceName);
    Serial.println("[BLE] Device initialized");

    for (int i = 0; i < WHEEL_COUNT; i++) {
        _callbacks[i].wheelIdx = (uint8_t)i;
    }
}

// ---------------------------------------------------------------------------
// Connect to a single wheel and run the M25 protocol init sequence.
// Blocking (uses delay()) - only called during STATE_CONNECTING.
// ---------------------------------------------------------------------------
static bool _connectWheel(int idx) {
    WheelConnState_t &w = _wheels[idx];
    w.telegramId    = M25_TELEGRAM_ID_START;
    w.driveModeBits = 0;
    w.protocolReady = false;

    Serial.printf("[BLE] Connecting to %s wheel (%s)...\n", w.name, w.mac);

    if (w.client == nullptr) {
        w.client = BLEDevice::createClient();
        w.client->setClientCallbacks(&_callbacks[idx]);
    }

    if (!w.client->connect(BLEAddress(w.mac))) {
        Serial.printf("[BLE] %s wheel: GATT connect FAILED\n", w.name);
        return false;
    }

    BLERemoteService* svc = w.client->getService(BLEUUID(M25_SPP_SERVICE_UUID));
    if (!svc) {
        Serial.printf("[BLE] %s wheel: SPP service not found\n", w.name);
        w.client->disconnect();
        return false;
    }

    w.rxChar = svc->getCharacteristic(BLEUUID(M25_CHAR_RX_UUID));
    if (!w.rxChar) {
        Serial.printf("[BLE] %s wheel: RX characteristic not found\n", w.name);
        w.client->disconnect();
        return false;
    }

    w.connected = true;

    // M25 protocol init sequence (m25_parking.py connect())
    // Step 0: WRITE_SYSTEM_MODE = 0x01 (initialize communication)
    uint8_t sysMode = M25_SYSTEM_MODE_CONNECT;
    _sendCommand(idx, M25_SRV_APP_MGMT, M25_PARAM_WRITE_SYSTEM_MODE, &sysMode, 1);
    delay(300);   // wait for ACK per m25_parking.py (blocking, only at connect)

    // Step 1: WRITE_DRIVE_MODE = 0x04 (enable remote bit)
    uint8_t driveMode = M25_DRIVE_MODE_REMOTE;
    _sendCommand(idx, M25_SRV_APP_MGMT, M25_PARAM_WRITE_DRIVE_MODE, &driveMode, 1);
    w.driveModeBits = M25_DRIVE_MODE_REMOTE;
    delay(300);

    w.protocolReady = true;
    Serial.printf("[BLE] %s wheel ready\n", w.name);
    return true;
}

// ---------------------------------------------------------------------------
// Connect to both wheels (call after bleInit)
// ---------------------------------------------------------------------------
inline void bleConnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheels[i].connected) {
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
        WheelConnState_t &w = _wheels[i];
        if (w.connected && w.rxChar) {
            // Step 3: WRITE_REMOTE_SPEED = 0  (stop)
            uint8_t spd[2] = { 0, 0 };
            _sendCommand(i, M25_SRV_APP_MGMT, M25_PARAM_WRITE_REMOTE_SPEED, spd, 2);
            delay(50);
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

inline bool bleAllConnected() {
    return bleIsConnected(WHEEL_LEFT) && bleIsConnected(WHEEL_RIGHT);
}

inline bool bleAnyConnected() {
    return bleIsConnected(WHEEL_LEFT) || bleIsConnected(WHEEL_RIGHT);
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
    uint32_t now = millis();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        WheelConnState_t &w = _wheels[i];
        if (!w.connected) {
            if (now - w.lastConnectAttemptMs >= BLE_RECONNECT_DELAY_MS) {
                w.lastConnectAttemptMs = now;
                Serial.printf("[BLE] Attempting reconnect to %s wheel...\n",
                              w.name);
                _connectWheel(i);
            }
        }
    }
}

#endif // M25_BLE_H
