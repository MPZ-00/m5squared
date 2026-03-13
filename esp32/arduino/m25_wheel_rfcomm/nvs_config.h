/*
 * nvs_config.h - Persistent wheel-side configuration for m25_wheel_rfcomm.
 *
 * Stores the selected wheel side in ESP32 NVS so the same firmware can be
 * uploaded once, then switched to left/right over Serial with:
 *   config set left
 *   config set right
 *
 * If NVS does not contain a saved side, the sketch falls back to the compile-
 * time default from config.h.
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

#define WHEELCFG_NAMESPACE "m25wheel"

struct WheelRuntimeConfig {
    uint8_t side;
    bool sideFromNvs;
    char deviceName[24];
#if TRANSPORT_BLE_ENABLED
    char bleServiceUuid[37];
    char bleTxUuid[37];
    char bleRxUuid[37];
#endif
    uint8_t key[16];
};

// Static side-specific profile table used to populate runtime config.
struct WheelStaticProfile {
    const char* deviceName;
#if TRANSPORT_BLE_ENABLED
    const char* bleServiceUuid;
    const char* bleTxUuid;
    const char* bleRxUuid;
#endif
    const uint8_t* key;
};

static const uint8_t _wheelCfgKeyLeft[16]  = ENCRYPTION_KEY_LEFT;
static const uint8_t _wheelCfgKeyRight[16] = ENCRYPTION_KEY_RIGHT;

static const WheelStaticProfile _wheelCfgProfiles[2] = {
    {
        DEVICE_NAME_LEFT,
#if TRANSPORT_BLE_ENABLED
        BLE_SERVICE_UUID_LEFT,
        BLE_CHAR_UUID_TX_LEFT,
        BLE_CHAR_UUID_RX_LEFT,
#endif
        _wheelCfgKeyLeft,
    },
    {
        DEVICE_NAME_RIGHT,
#if TRANSPORT_BLE_ENABLED
        BLE_SERVICE_UUID_RIGHT,
        BLE_CHAR_UUID_TX_RIGHT,
        BLE_CHAR_UUID_RX_RIGHT,
#endif
        _wheelCfgKeyRight,
    },
};

inline bool wheelcfg_is_valid_side(uint8_t side) {
    return side == WHEEL_SIDE_LEFT || side == WHEEL_SIDE_RIGHT;
}

inline const char* wheelcfg_side_name(uint8_t side) {
    return side == WHEEL_SIDE_RIGHT ? "right" : "left";
}

inline uint8_t wheelcfg_compiled_default_side() {
    return WHEEL_SIDE_DEFAULT;
}

inline const WheelStaticProfile* wheelcfg_profile_for_side(uint8_t side) {
    if (side == WHEEL_SIDE_RIGHT) return &_wheelCfgProfiles[WHEEL_SIDE_RIGHT];
    return &_wheelCfgProfiles[WHEEL_SIDE_LEFT];
}

inline void wheelcfg_fill_runtime(WheelRuntimeConfig* cfg, uint8_t side, bool sideFromNvs) {
    if (!cfg) return;

    const uint8_t resolvedSide = (side == WHEEL_SIDE_RIGHT)
                               ? WHEEL_SIDE_RIGHT
                               : WHEEL_SIDE_LEFT;
    const WheelStaticProfile* profile = wheelcfg_profile_for_side(resolvedSide);

    cfg->side = resolvedSide;
    cfg->sideFromNvs = sideFromNvs;

    strlcpy(cfg->deviceName, profile->deviceName, sizeof(cfg->deviceName));
#if TRANSPORT_BLE_ENABLED
    strlcpy(cfg->bleServiceUuid,
            profile->bleServiceUuid,
            sizeof(cfg->bleServiceUuid));
    strlcpy(cfg->bleTxUuid,
            profile->bleTxUuid,
            sizeof(cfg->bleTxUuid));
    strlcpy(cfg->bleRxUuid,
            profile->bleRxUuid,
            sizeof(cfg->bleRxUuid));
#endif
    memcpy(cfg->key, profile->key, sizeof(cfg->key));
}

inline bool wheelcfg_load_side(uint8_t* sideOut) {
    if (!sideOut) return false;

    Preferences prefs;
    if (!prefs.begin(WHEELCFG_NAMESPACE, /*readOnly=*/false)) {
        *sideOut = WHEEL_SIDE_DEFAULT;
        return false;
    }

    const uint8_t stored = prefs.getUChar("side", 0xFF);
    prefs.end();

    if (wheelcfg_is_valid_side(stored)) {
        *sideOut = stored;
        return true;
    }

    *sideOut = WHEEL_SIDE_DEFAULT;
    return false;
}

inline void wheelcfg_load(WheelRuntimeConfig* cfg) {
    uint8_t side = WHEEL_SIDE_DEFAULT;
    const bool fromNvs = wheelcfg_load_side(&side);
    wheelcfg_fill_runtime(cfg, side, fromNvs);
}

inline bool wheelcfg_save_side(uint8_t side) {
    if (!wheelcfg_is_valid_side(side)) return false;

    Preferences prefs;
    if (!prefs.begin(WHEELCFG_NAMESPACE, /*readOnly=*/false)) return false;
    const bool ok = prefs.putUChar("side", side) == 1;
    prefs.end();
    return ok;
}

inline bool wheelcfg_clear() {
    Preferences prefs;
    if (!prefs.begin(WHEELCFG_NAMESPACE, /*readOnly=*/false)) return false;
    prefs.clear();
    prefs.end();
    return true;
}

inline void wheelcfg_print(const WheelRuntimeConfig* cfg) {
    if (!cfg) {
        Serial.println(F("[Config] Runtime config unavailable"));
        return;
    }

    Serial.println(F("[Config] --- Wheel Configuration ---"));
    Serial.printf("[Config] Active side     : %s  (%s)\n",
                  wheelcfg_side_name(cfg->side),
                  cfg->sideFromNvs ? "NVS" : "build default");
    Serial.printf("[Config] Default side    : %s\n",
                  wheelcfg_side_name(wheelcfg_compiled_default_side()));
    Serial.printf("[Config] Device name     : %s\n", cfg->deviceName);
#if TRANSPORT_BLE_ENABLED
    Serial.printf("[Config] BLE service UUID: %s\n", cfg->bleServiceUuid);
    Serial.printf("[Config] BLE TX UUID     : %s\n", cfg->bleTxUuid);
    Serial.printf("[Config] BLE RX UUID     : %s\n", cfg->bleRxUuid);
#endif
    Serial.print(F("[Config] Active key      : "));
    for (size_t i = 0; i < sizeof(cfg->key); i++) Serial.printf("%02X", cfg->key[i]);
    Serial.println();
    Serial.println(F("[Config] 'config set left|right' saves to NVS and reboots."));
    Serial.println(F("[Config] 'config reset' clears NVS and reboots."));
}

#endif // NVS_CONFIG_H