/*
 * m25_ble.h - BLE client for Alber e-motion M25 wheels
 *
 * Manages BLE connections to up to two M25 wheels and provides a simple
 * command interface for the remote control sketch.
 *
 * Protocol references (see Python codebase):
 *   m25_protocol.py        - packet format and command IDs
 *   m25_crypto.py          - AES-128 encryption details
 *   m25_bluetooth.py       - SPP UUID and connection procedure
 *
 * This file implements the connection and transport layer.
 * Actual M25 packet construction is marked TODO where the Python
 * protocol logic must be ported to C++.
 *
 * UUIDs: M25 wheels use Bluetooth SPP-style UUIDs over BLE.
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
#include "device_config.h"

// ---------------------------------------------------------------------------
// M25 SPP Service / Characteristic UUIDs
// From m5squared/m25_bluetooth.py and fake_m25_wheel.ino
// ---------------------------------------------------------------------------
#define M25_SPP_SERVICE_UUID  "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_TX_UUID      "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_RX_UUID      "00001102-0000-1000-8000-00805F9B34FB"

// M25 BLE SPP channel (from Python: RFCOMM_CHANNEL = 6)
// Over BLE this maps to the characteristic; kept as documentation reference.
#define M25_SPP_CHANNEL 6

// ---------------------------------------------------------------------------
// Wheel slot indices
// ---------------------------------------------------------------------------
#define WHEEL_LEFT  0
#define WHEEL_RIGHT 1
#define WHEEL_COUNT 2

// ---------------------------------------------------------------------------
// Wheel connection state
// ---------------------------------------------------------------------------
enum WheelConnState : uint8_t {
    WHEEL_DISCONNECTED = 0,
    WHEEL_SCANNING,
    WHEEL_CONNECTING,
    WHEEL_CONNECTED,
    WHEEL_ERROR
};

struct WheelConnection {
    const char*                 mac;
    const char*                 name;
    const uint8_t*              key;            // 16-byte AES key
    WheelConnState              state;
    BLEClient*                  client;
    BLERemoteCharacteristic*    rxChar;         // write commands here
    BLERemoteCharacteristic*    txChar;         // receive notifications here
    uint32_t                    lastCommandMs;
    uint32_t                    lastConnectAttemptMs;
};

// ---------------------------------------------------------------------------
// Encryption keys (from device_config.h)
// ---------------------------------------------------------------------------
static const uint8_t _keyLeft[16]  = ENCRYPTION_KEY_LEFT;
static const uint8_t _keyRight[16] = ENCRYPTION_KEY_RIGHT;

// ---------------------------------------------------------------------------
// Global wheel state
// ---------------------------------------------------------------------------
static WheelConnection _wheels[WHEEL_COUNT] = {
    { LEFT_WHEEL_MAC,  "Left",  _keyLeft,  WHEEL_DISCONNECTED, nullptr, nullptr, nullptr, 0, 0 },
    { RIGHT_WHEEL_MAC, "Right", _keyRight, WHEEL_DISCONNECTED, nullptr, nullptr, nullptr, 0, 0 },
};

// ---------------------------------------------------------------------------
// AES-128 ECB encryption (same as existing wifi_joystick/encryption.h)
// ---------------------------------------------------------------------------
static bool _encryptBlock(const uint8_t* key, const uint8_t* plaintext,
                           uint8_t* ciphertext) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    int ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT,
                                    plaintext, ciphertext);
    mbedtls_aes_free(&ctx);
    return (ret == 0);
}

// ---------------------------------------------------------------------------
// BLE disconnect callback
// ---------------------------------------------------------------------------
class M25DisconnectCallback : public BLEClientCallbacks {
public:
    uint8_t wheelIdx;
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override {
        Serial.printf("[BLE] Wheel %s disconnected\n",
                      _wheels[wheelIdx].name);
        _wheels[wheelIdx].state = WHEEL_DISCONNECTED;
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
// Connect to a single wheel
// Returns true if the connection and characteristic discovery succeeded.
// ---------------------------------------------------------------------------
static bool _connectWheel(int idx) {
    WheelConnection &w = _wheels[idx];
    Serial.printf("[BLE] Connecting to %s wheel (%s)...\n", w.name, w.mac);
    w.state = WHEEL_CONNECTING;

    BLEAddress addr(w.mac);
    if (w.client == nullptr) {
        w.client = BLEDevice::createClient();
        w.client->setClientCallbacks(&_callbacks[idx]);
    }

    if (!w.client->connect(addr)) {
        Serial.printf("[BLE] Connection to %s wheel FAILED\n", w.name);
        w.state = WHEEL_ERROR;
        return false;
    }

    // Discover the SPP service
    BLERemoteService* svc =
        w.client->getService(BLEUUID(M25_SPP_SERVICE_UUID));
    if (svc == nullptr) {
        Serial.printf("[BLE] SPP service not found on %s wheel\n", w.name);
        w.client->disconnect();
        w.state = WHEEL_ERROR;
        return false;
    }

    w.rxChar = svc->getCharacteristic(BLEUUID(M25_CHAR_RX_UUID));
    w.txChar = svc->getCharacteristic(BLEUUID(M25_CHAR_TX_UUID));

    if (w.rxChar == nullptr) {
        Serial.printf("[BLE] RX characteristic not found on %s wheel\n", w.name);
        w.client->disconnect();
        w.state = WHEEL_ERROR;
        return false;
    }

    w.state = WHEEL_CONNECTED;
    w.lastCommandMs = millis();
    Serial.printf("[BLE] %s wheel connected OK\n", w.name);
    return true;
}

// ---------------------------------------------------------------------------
// Connect to both wheels (call after bleInit)
// ---------------------------------------------------------------------------
inline void bleConnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_wheels[i].state != WHEEL_CONNECTED) {
            _connectWheel(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Disconnect all wheels
// ---------------------------------------------------------------------------
inline void bleDisconnect() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        WheelConnection &w = _wheels[i];
        if (w.client && w.client->isConnected()) {
            w.client->disconnect();
        }
        w.state = WHEEL_DISCONNECTED;
    }
}

// ---------------------------------------------------------------------------
// Connection status queries
// ---------------------------------------------------------------------------
inline bool bleIsConnected(int wheelIdx) {
    return _wheels[wheelIdx].state == WHEEL_CONNECTED
        && _wheels[wheelIdx].client != nullptr
        && _wheels[wheelIdx].client->isConnected();
}

inline bool bleAllConnected() {
    return bleIsConnected(WHEEL_LEFT) && bleIsConnected(WHEEL_RIGHT);
}

inline bool bleAnyConnected() {
    return bleIsConnected(WHEEL_LEFT) || bleIsConnected(WHEEL_RIGHT);
}

// ---------------------------------------------------------------------------
// Send a raw 16-byte encrypted packet to a wheel's RX characteristic
// ---------------------------------------------------------------------------
static bool _sendPacket(int idx, const uint8_t* plain16) {
    WheelConnection &w = _wheels[idx];
    if (!bleIsConnected(idx) || w.rxChar == nullptr) return false;

    uint8_t cipher[16];
    if (!_encryptBlock(w.key, plain16, cipher)) {
        Serial.printf("[BLE] Encryption error for %s wheel\n", w.name);
        return false;
    }
    w.rxChar->writeValue(cipher, 16, false);
    w.lastCommandMs = millis();
    return true;
}

// ---------------------------------------------------------------------------
// Build and send a STOP command
//
// TODO: Replace placeholder packet with correct M25 protocol format.
//       Reference: m25_protocol.py -> build_stop_packet() / SpeedCommand
//       The M25 SPP packet format (from m25-protocol.md):
//         Byte 0   : Command ID  (e.g. 0x21 for speed control)
//         Byte 1-2 : Left wheel speed  (int16, signed, unit: 0.01 km/h)
//         Byte 3-4 : Right wheel speed (int16, signed)
//         Byte 5-15: Padding / checksum (protocol-specific)
// ---------------------------------------------------------------------------
inline bool bleSendStop() {
    uint8_t pkt[16] = { 0 };
    // TODO: fill pkt with correct M25 stop command bytes
    pkt[0] = 0x21;   // Placeholder command ID (speed control)
    // Left speed  = 0 (bytes 1-2 are already 0)
    // Right speed = 0 (bytes 3-4 are already 0)

    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendPacket(i, pkt);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Send motor speed command to both wheels
//
//  leftSpeed  : -100 ... +100 (percent of Vmax, positive = forward)
//  rightSpeed : -100 ... +100
//
// TODO: Replace placeholder packet with correct M25 protocol encoding.
//       Reference: m25_protocol.py -> SpeedCommand / build_movement_packet()
// ---------------------------------------------------------------------------
inline bool bleSendMotorCommand(float leftSpeed, float rightSpeed) {
    // Convert percent (-100..+100) to M25 protocol units.
    // TODO: Verify unit conversion. Python codebase uses signed 16-bit values
    //       in units of 0.01 km/h. With VMAX_OUTDOOR = 8 km/h at 80 %:
    //       800 raw units <-> 100 % command value.
    int16_t leftRaw  = (int16_t)(leftSpeed  * 8.0f);   // placeholder scale
    int16_t rightRaw = (int16_t)(rightSpeed * 8.0f);

    uint8_t pkt[16] = { 0 };
    pkt[0] = 0x21;                              // TODO: correct command ID
    pkt[1] = (uint8_t)( leftRaw        & 0xFF);
    pkt[2] = (uint8_t)((leftRaw >> 8)  & 0xFF);
    pkt[3] = (uint8_t)( rightRaw       & 0xFF);
    pkt[4] = (uint8_t)((rightRaw >> 8) & 0xFF);
    // pkt[5..15]: checksum / sequence / flags - TODO from protocol spec

    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendPacket(i, pkt);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Send hill hold command
//
// TODO: Implement correct M25 hill hold packet.
//       Reference: m25_parking.py / m25_protocol.py -> HillHoldCommand
// ---------------------------------------------------------------------------
inline bool bleSendHillHold(bool enable) {
    uint8_t pkt[16] = { 0 };
    pkt[0] = 0x30;          // TODO: correct command ID for hill hold
    pkt[1] = enable ? 0x01 : 0x00;

    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendPacket(i, pkt);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Send assist level command
//
//  level: ASSIST_INDOOR(0), ASSIST_OUTDOOR(1), ASSIST_LEARNING(2)
//
// TODO: Map to correct M25 drive profile IDs.
//       Reference: m25_ecs_driveprofiles.py / m25_protocol.py -> ProfileCommand
//       Known profiles from Python code: Normal=1, Outdoor=2, Learning=3
// ---------------------------------------------------------------------------
inline bool bleSendAssistLevel(uint8_t level) {
    // M25 profile IDs: 1=normal/indoor, 2=outdoor, 3=learning
    static const uint8_t profileIds[ASSIST_COUNT] = { 1, 2, 3 };
    uint8_t profileId = (level < ASSIST_COUNT) ? profileIds[level] : 1;

    uint8_t pkt[16] = { 0 };
    pkt[0] = 0x31;          // TODO: correct command ID for profile/assist
    pkt[1] = profileId;

    bool ok = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (bleIsConnected(i)) {
            ok &= _sendPacket(i, pkt);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Background tick: attempt reconnect if a wheel dropped out
// Call from loop() but no more often than every BLE_RECONNECT_DELAY ms.
// ---------------------------------------------------------------------------
#define BLE_RECONNECT_DELAY_MS 5000

inline void bleTick() {
    uint32_t now = millis();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        WheelConnection &w = _wheels[i];
        if (w.state == WHEEL_DISCONNECTED || w.state == WHEEL_ERROR) {
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
