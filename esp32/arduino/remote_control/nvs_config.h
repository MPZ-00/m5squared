/*
 * nvs_config.h - Non-volatile storage for per-device wheel configuration.
 *
 * Wheel MACs and AES keys are stored in the ESP32 NVS (flash) under the
 * "m25cfg" namespace using the Arduino Preferences library.
 *
 * On first boot (NVS empty) the compiled-in defaults from device_config.h
 * (or injected by load_env.py / build flags) are used automatically.
 *
 * Usage in setup():
 *   char lmac[18], rmac[18];
 *   uint8_t lkey[16], rkey[16];
 *   nvsLoadMac(WHEEL_LEFT,  lmac, sizeof(lmac));
 *   nvsLoadMac(WHEEL_RIGHT, rmac, sizeof(rmac));
 *   nvsLoadKey(WHEEL_LEFT,  lkey);
 *   nvsLoadKey(WHEEL_RIGHT, rkey);
 *   supervisor.requestConnect(lmac, rmac, lkey, rkey);
 *
 * Serial commands (wired in serial_commands.h):
 *   config show               Print MACs and keys, flag NVS vs compiled default
 *   config reset              Clear NVS; compiled defaults take effect on reboot
 *   setmac left  <MAC>        Change + persist left  wheel MAC
 *   setmac right <MAC>        Change + persist right wheel MAC
 *   setkey left  <32hex>      Change + persist left  wheel AES key
 *   setkey right <32hex>      Change + persist right wheel AES key
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include "device_config.h"
#include "m25_ble.h"   // WHEEL_LEFT / WHEEL_RIGHT

#define NVS_NAMESPACE "m25cfg"

// ---------------------------------------------------------------------------
// Compiled-in fallbacks (from device_config.h, possibly overridden by build
// flags injected by load_env.py)
// ---------------------------------------------------------------------------
static const char    _nvsDfltLMac[] = LEFT_WHEEL_MAC;
static const char    _nvsDfltRMac[] = RIGHT_WHEEL_MAC;
static const uint8_t _nvsDfltLKey[] = ENCRYPTION_KEY_LEFT;
static const uint8_t _nvsDfltRKey[] = ENCRYPTION_KEY_RIGHT;

// ---------------------------------------------------------------------------
// Load MAC for wheel idx into buf (must be >= 18 chars).
// Returns true if an NVS value was found, false if using the compiled default.
// ---------------------------------------------------------------------------
inline bool nvsLoadMac(int idx, char* buf, size_t bufLen) {
    Preferences p;
    // Open RW so missing namespace is created silently on first boot.
    // Opening RO on a missing namespace logs noisy NOT_FOUND errors.
    if (!p.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        strlcpy(buf, (idx == WHEEL_LEFT) ? _nvsDfltLMac : _nvsDfltRMac, bufLen);
        return false;
    }
    String val = p.getString((idx == WHEEL_LEFT) ? "lmac" : "rmac", "");
    p.end();
    if (val.length() == 17) {
        strlcpy(buf, val.c_str(), bufLen);
        return true;
    }
    strlcpy(buf, (idx == WHEEL_LEFT) ? _nvsDfltLMac : _nvsDfltRMac, bufLen);
    return false;
}

// ---------------------------------------------------------------------------
// Load AES key for wheel idx into dest (exactly 16 bytes).
// Returns true if an NVS value was found, false if using the compiled default.
// ---------------------------------------------------------------------------
inline bool nvsLoadKey(int idx, uint8_t* dest) {
    Preferences p;
    // Open RW so missing namespace is created silently on first boot.
    if (!p.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        memcpy(dest, (idx == WHEEL_LEFT) ? _nvsDfltLKey : _nvsDfltRKey, 16);
        return false;
    }
    const char* nvKey = (idx == WHEEL_LEFT) ? "lkey" : "rkey";
    size_t n   = p.getBytesLength(nvKey);
    bool found = (n == 16) && (p.getBytes(nvKey, dest, 16) == 16);
    p.end();
    if (!found) {
        memcpy(dest, (idx == WHEEL_LEFT) ? _nvsDfltLKey : _nvsDfltRKey, 16);
    }
    return found;
}

// ---------------------------------------------------------------------------
// Persist a MAC address (must be "XX:XX:XX:XX:XX:XX", 17 chars).
// Returns true on success.
// ---------------------------------------------------------------------------
inline bool nvsSaveMac(int idx, const char* mac) {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;
    bool ok = p.putString((idx == WHEEL_LEFT) ? "lmac" : "rmac", mac) > 0;
    p.end();
    return ok;
}

// ---------------------------------------------------------------------------
// Persist a 16-byte AES key.
// Returns true on success.
// ---------------------------------------------------------------------------
inline bool nvsSaveKey(int idx, const uint8_t* key16) {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;
    bool ok = p.putBytes((idx == WHEEL_LEFT) ? "lkey" : "rkey", key16, 16) == 16;
    p.end();
    return ok;
}

// ---------------------------------------------------------------------------
// Erase all NVS config; compiled-in defaults take effect on next reboot.
// ---------------------------------------------------------------------------
inline void nvsClearAll() {
    Preferences p;
    p.begin(NVS_NAMESPACE, /*readOnly=*/false);
    p.clear();
    p.end();
}

// ---------------------------------------------------------------------------
// Print current effective config to Serial, noting NVS vs compiled default.
// ---------------------------------------------------------------------------
inline void nvsPrintAll() {
    char    lmac[18], rmac[18];
    uint8_t lkey[16], rkey[16];
    bool lmacNvs = nvsLoadMac(WHEEL_LEFT,  lmac, sizeof(lmac));
    bool rmacNvs = nvsLoadMac(WHEEL_RIGHT, rmac, sizeof(rmac));
    bool lkeyNvs = nvsLoadKey(WHEEL_LEFT,  lkey);
    bool rkeyNvs = nvsLoadKey(WHEEL_RIGHT, rkey);

    Serial.println(F("[Config] --- Wheel Configuration ---"));
    Serial.printf("[Config] Left  MAC : %s  (%s)\n", lmac, lmacNvs ? "NVS" : "build default");
    Serial.printf("[Config] Right MAC : %s  (%s)\n", rmac, rmacNvs ? "NVS" : "build default");

    Serial.print(F("[Config] Left  Key : "));
    for (int i = 0; i < 16; i++) Serial.printf("%02x", lkey[i]);
    Serial.printf("  (%s)\n", lkeyNvs ? "NVS" : "build default");

    Serial.print(F("[Config] Right Key : "));
    for (int i = 0; i < 16; i++) Serial.printf("%02x", rkey[i]);
    Serial.printf("  (%s)\n", rkeyNvs ? "NVS" : "build default");

    Serial.println(F("[Config] Changes take effect immediately AND survive reboot."));
    Serial.println(F("[Config] 'config reset' clears NVS; build defaults restored on next boot."));
}

#endif // NVS_CONFIG_H
