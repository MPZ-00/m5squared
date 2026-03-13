/**
 * m25_wheel_rfcomm.ino - Fake M25 Wheel Simulator (RFCOMM / SPP edition)
 *
 * This file contains ONLY setup() and loop().
 * All logic lives in the header modules:
 *
 *   config.h          - Compile-time settings (device name, key, pins)
 *   state.h           - WheelState struct and mutation helpers
 *   protocol.h        - CRC-16, byte stuffing, frame encode/decode
 *   crypto.h          - AES-128 ECB / CBC wrappers
 *   packet.h          - Full M25 packet decode (incoming) and encode (ACK)
 *   command.h         - Parse SPP bytes -> update WheelState
 *   transport_rfcomm.h- Bluetooth Classic SPP transport
 *   transport_ble.h   - BLE GATT transport (optional)
 *   led.h             - LED visual feedback
 *   buzzer.h          - Buzzer audio feedback
 *   cli.h             - Serial command-line interface
 *   safety.h          - Command timeout, battery drain, button handler
 *
 * Hardware: ESP32-WROOM-32 or compatible
 * Transport: Bluetooth Classic SPP (RFCOMM channel 6) + optional BLE GATT
 */

#include "config.h"
#include "nvs_config.h"
#include "state.h"
#include "protocol.h"
#include "crypto.h"
#include "packet.h"
#include "command.h"
#include "transport_rfcomm.h"
#include "transport_ble.h"
#include "led.h"
#include "buzzer.h"
#include "cli.h"
#include "safety.h"

// ---------------------------------------------------------------------------
// Wheel state - single global instance
// ---------------------------------------------------------------------------
static WheelState wheel;
static WheelRuntimeConfig wheelCfg;

// ---------------------------------------------------------------------------
// CLI action callbacks
// ---------------------------------------------------------------------------
static void do_send_response();
static void do_send_response_for(uint8_t reqService, uint8_t reqParam);
static void do_disconnect();
static void do_advertise();
static bool is_connected();

static CliActions cliAct = {
    .sendResponse = do_send_response,
    .disconnect   = do_disconnect,
    .advertise    = do_advertise,
    .connected    = is_connected,
    .key          = nullptr,
    .wheelConfig  = &wheelCfg,
};

// ---------------------------------------------------------------------------
// do_process_frame - decode a raw M25 frame and act on it.
//   Called for frames received by any active transport.
//   Returns true when the frame decoded and was acted on successfully.
// ---------------------------------------------------------------------------
static bool do_process_frame(const uint8_t* raw, size_t rawLen) {
    if (cli_dbg(DBG_RAW_DATA)) {
        proto_print_hex("[RX] Raw", raw, rawLen);
    }

    // Decode: parse frame header/CRC, decrypt payload, strip padding
    uint8_t spp[64];
    size_t  sppLen = 0;
    if (!packet_decode(raw, rawLen, wheelCfg.key, spp, &sppLen)) {
        if (cli_dbg(DBG_PROTOCOL)) {
            Serial.println(F("[RX] Decode FAILED (CRC mismatch or bad crypto)"));
        }
        return false;
    }

    if (cli_dbg(DBG_RAW_DATA)) {
        proto_print_hex("[RX] SPP", spp, sppLen);
    }

    // Apply command to wheel state
    const CmdResult result = command_apply(spp, sppLen, &wheel,
                                           cli_dbg(DBG_COMMANDS));

    // Send ACK - except for high-rate REMOTE_SPEED (would flood the link).
    // Mirror service/param so the app can route the reply correctly.
    if (result == CMD_SEND_ACK) {
        do_send_response_for(spp[4], spp[5]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// _transport_send - push a pre-built frame to all active transports.
// ---------------------------------------------------------------------------
static void _transport_send(const uint8_t* frame, size_t frameLen) {
    if (cli_dbg(DBG_RAW_DATA)) proto_print_hex("[TX] Frame", frame, frameLen);
#if TRANSPORT_RFCOMM_ENABLED
    if (rfcomm_connected()) rfcomm_send(frame, frameLen);
#endif
#if TRANSPORT_BLE_ENABLED
    if (ble_connected()) ble_send(frame, frameLen);
#endif
}

// ---------------------------------------------------------------------------
// do_send_response - generic ACK (used by CLI; service=APP_MGMT param=0xFF).
// ---------------------------------------------------------------------------
static void do_send_response() {
    uint8_t frame[128];
    const size_t frameLen = packet_encode_ack(&wheel, wheelCfg.key, frame);
    if (frameLen == 0) { Serial.println(F("[TX] ERROR: packet_encode_ack failed")); return; }
    _transport_send(frame, frameLen);
}

// ---------------------------------------------------------------------------
// do_send_response_for - context-aware reply that mirrors back service/param.
// ---------------------------------------------------------------------------
static void do_send_response_for(uint8_t reqService, uint8_t reqParam) {
    uint8_t frame[128];
    const size_t frameLen = packet_encode_response(reqService, reqParam, &wheel, wheelCfg.key, frame);
    if (frameLen == 0) { Serial.println(F("[TX] ERROR: packet_encode_response failed")); return; }
    _transport_send(frame, frameLen);
}

// ---------------------------------------------------------------------------
// do_disconnect - disconnect active transport clients.
// ---------------------------------------------------------------------------
static void do_disconnect() {
#if TRANSPORT_RFCOMM_ENABLED
    Serial.println(F("[RFCOMM] Disconnect not supported by BluetoothSerial"));
#endif
#if TRANSPORT_BLE_ENABLED
    if (ble_connected()) ble_disconnect();
#endif
}

// ---------------------------------------------------------------------------
// do_advertise - restart advertising on all transports.
// ---------------------------------------------------------------------------
static void do_advertise() {
#if TRANSPORT_BLE_ENABLED
    ble_start_advertising();
    Serial.println(F("[BLE] Advertising restarted"));
#endif
    // RFCOMM is permanently discoverable once started; nothing to do
}

// ---------------------------------------------------------------------------
// is_connected - true if any transport has an active client.
// ---------------------------------------------------------------------------
static bool is_connected() {
#if TRANSPORT_RFCOMM_ENABLED
    if (rfcomm_connected()) return true;
#endif
#if TRANSPORT_BLE_ENABLED
    if (ble_connected())    return true;
#endif
    return false;
}

// ---------------------------------------------------------------------------
// on_connect / on_disconnect - shared transport event handlers.
// ---------------------------------------------------------------------------
static void on_connect() {
    led_set_connected();
    buzzer_beep(2);
    Serial.println(F("=== CLIENT CONNECTED ==="));

    // Reset state that should not carry over between sessions
    wheel.speed        = 0;
    wheel.lastCmdMs    = 0;
}

static void on_disconnect() {
    led_set_advertising();
    buzzer_beep(1);
    Serial.println(F("=== CLIENT DISCONNECTED ==="));
    wheel.speed        = 0;
    wheel.lastCmdMs    = 0;
}

// ---------------------------------------------------------------------------
// Battery drain timer
// ---------------------------------------------------------------------------
static unsigned long _lastBatteryMs = 0;

static void tick_battery() {
    const unsigned long interval = safety_battery_interval(&wheel);
    if (millis() - _lastBatteryMs < interval) return;
    _lastBatteryMs = millis();

    const int oldLevel = wheel.battery;
    safety_battery_tick(&wheel);

    // Update battery LED only on threshold boundary
    const int oldT = (oldLevel > 66) ? 2 : (oldLevel > 33) ? 1 : 0;
    const int newT = (wheel.battery > 66) ? 2 : (wheel.battery > 33) ? 1 : 0;
    if (oldT != newT) led_show_battery(wheel.battery);
}

// ===========================================================================
// setup
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(1500);

    wheelcfg_load(&wheelCfg);
    cliAct.key = wheelCfg.key;

    Serial.println(F("\n=== M25 Wheel RFCOMM Simulator ==="));
    Serial.printf("Device: %s\n", wheelCfg.deviceName);
    Serial.printf("Wheel side: %s (%s)\n",
                  wheelcfg_side_name(wheelCfg.side),
                  wheelCfg.sideFromNvs ? "NVS" : "build default");
    Serial.println();

    // Hardware
    led_init();
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    buzzer_init();
    buzzer_test();

    // Initial LED state
    led_show_battery(wheel.battery);
    led_set_advertising();

    // State
    state_init(&wheel);

    // RFCOMM transport
#if TRANSPORT_RFCOMM_ENABLED
    rfcomm_init(wheelCfg.deviceName);
#endif

    // BLE transport
#if TRANSPORT_BLE_ENABLED
    ble_init(wheelCfg.deviceName,
             wheelCfg.bleServiceUuid,
             wheelCfg.bleTxUuid,
             wheelCfg.bleRxUuid);
#endif

    Serial.println(F("Ready. Type 'help' for commands."));
    wheelcfg_print(&wheelCfg);
    Serial.println();
}

// ===========================================================================
// loop
// ===========================================================================
void loop() {
    // -- Transport event detection -----------------------------------------
#if TRANSPORT_RFCOMM_ENABLED
    if (rfcomm_check_events()) {
        if (rfcomm_connected()) on_connect();
        else                    on_disconnect();
    }
#endif

#if TRANSPORT_BLE_ENABLED
    ble_check_events(on_connect, on_disconnect);
#endif

    // -- Receive: drain all available frames from RFCOMM -------------------
#if TRANSPORT_RFCOMM_ENABLED
    {
        uint8_t frame[128];
        size_t  frameLen = 0;
        while (rfcomm_poll(frame, &frameLen)) {
            do_process_frame(frame, frameLen);
        }
    }
#endif

    // -- Receive: drain all available frames from BLE ----------------------
#if TRANSPORT_BLE_ENABLED
    {
        uint8_t frame[128];
        size_t  frameLen = 0;
        while (ble_poll(frame, &frameLen)) {
            const bool ok = do_process_frame(frame, frameLen);
            if (!ble_first_valid()) {
                if (ok) {
                    if (cli_dbg(DBG_STALE) && ble_stale_count() > 0) {
                        Serial.printf("[BLE] First valid packet after %u stale (%lums after connect)\n",
                                      ble_stale_count(), millis() - ble_conn_time());
                    }
                    ble_mark_valid();
                } else {
                    ble_stale_inc();
                    const unsigned long elapsed = millis() - ble_conn_time();
                    if (elapsed > STALE_TIMEOUT_MS) {
                        // Always print: this is a real failure requiring reconnect
                        Serial.printf("[BLE] Timeout: %u stale packets over %lums - disconnecting\n",
                                      ble_stale_count(), elapsed);
                        ble_disconnect();
                    } else if (cli_dbg(DBG_STALE) && ble_stale_count() % 10 == 1) {
                        Serial.printf("[BLE] Stale: %u packets discarded, %lums after connect\n",
                                      ble_stale_count(), elapsed);
                    }
                }
            }
        }
    }
#endif

    // -- Safety: command timeout -------------------------------------------
    safety_cmd_timeout(&wheel);

    // -- Safety: button poll -----------------------------------------------
    safety_button_poll(do_advertise);

    // -- LED animation update ----------------------------------------------
    led_update();

    // -- LED + audio speed feedback ----------------------------------------
    if (_ledSpeedEnabled) led_set_speed_indicator(wheel.speed);
    buzzer_speed_tone(wheel.speed);

    // -- CLI: serial commands ----------------------------------------------
    cli_poll(&cliAct, &wheel);

    // -- Battery simulation ------------------------------------------------
    tick_battery();

    delay(10);
}
