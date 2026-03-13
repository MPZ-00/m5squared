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
#include <esp_mac.h>
#include <esp_system.h>

// ---------------------------------------------------------------------------
// Wheel state - single global instance
// ---------------------------------------------------------------------------
static WheelState wheel;
static WheelRuntimeConfig wheelCfg;

struct RuntimeStats {
    uint32_t rxFrames;
    uint32_t rxBytes;
    uint32_t decodeOk;
    uint32_t decodeFail;
    uint32_t speedUpdates;
    uint32_t ackSent;
    uint32_t txFrames;
    uint32_t txBytes;
    uint32_t connectEvents;
    uint32_t disconnectEvents;
    uint32_t advertiseRequests;
    uint32_t buttonAdvertiseRequests;
    uint32_t cmdTimeouts;
    uint32_t manualCliSends;
#if TRANSPORT_BLE_ENABLED
    uint32_t bleStaleDrops;
#endif
};

static RuntimeStats stats = {};

// ---------------------------------------------------------------------------
// CLI action callbacks
// ---------------------------------------------------------------------------
static void do_send_response();
static void do_send_response_for(uint8_t reqService, uint8_t reqParam);
static void do_disconnect();
static void do_advertise();
static bool is_connected();
static void print_mac();
static void print_whoami();
static void print_version();
static void print_uptime();
static void print_stats();

static CliActions cliAct = {
    .sendResponse = do_send_response,
    .disconnect   = do_disconnect,
    .advertise    = do_advertise,
    .connected    = is_connected,
    .printMac     = print_mac,
    .printWhoAmI  = print_whoami,
    .printVersion = print_version,
    .printUptime  = print_uptime,
    .printStats   = print_stats,
    .key          = nullptr,
    .wheelConfig  = &wheelCfg,
};

static const char* reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software restart";
        case ESP_RST_PANIC:     return "panic / exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "other";
    }
}

static void format_mac(char* out, size_t outLen, const uint8_t* mac) {
    if (!out || outLen < 18 || !mac) return;
    snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void print_mac() {
    uint8_t btMac[6] = {0};
    uint8_t baseMac[6] = {0};
    char btStr[18] = {0};
    char baseStr[18] = {0};

    esp_read_mac(btMac, ESP_MAC_BT);
    esp_read_mac(baseMac, ESP_MAC_EFUSE_FACTORY);
    format_mac(btStr, sizeof(btStr), btMac);
    format_mac(baseStr, sizeof(baseStr), baseMac);

    Serial.println(F("\n=== MAC Identity ==="));
    Serial.printf("BT MAC:   %s\n", btStr);
    Serial.printf("Base MAC: %s\n", baseStr);
    Serial.println(F("====================\n"));
}

static void print_whoami() {
    Serial.println(F("\n=== Device Identity ==="));
    Serial.printf("Device name:   %s\n", wheelCfg.deviceName);
    Serial.printf("Wheel side:    %s (%s)\n",
                  wheelcfg_side_name(wheelCfg.side),
                  wheelCfg.sideFromNvs ? "NVS" : "build default");
    Serial.printf("Default side:  %s\n",
                  wheelcfg_side_name(wheelcfg_compiled_default_side()));
    Serial.printf("Connected:     %s\n", is_connected() ? "yes" : "no");
#if TRANSPORT_RFCOMM_ENABLED
    Serial.printf("RFCOMM:        enabled, client=%s\n",
                  rfcomm_connected() ? "connected" : "idle");
    if (rfcomm_server_channel_known()) {
        Serial.printf("RFCOMM SCN:    %u\n", (unsigned)rfcomm_server_channel());
    } else {
        Serial.printf("RFCOMM SCN:    pending (config %d)\n", RFCOMM_CHANNEL);
    }
#else
    Serial.println(F("RFCOMM:        disabled"));
#endif
#if TRANSPORT_BLE_ENABLED
    Serial.printf("BLE:           enabled, client=%s\n",
                  ble_connected() ? "connected" : "idle");
#else
    Serial.println(F("BLE:           disabled"));
#endif
    print_mac();
}

static void print_version() {
    Serial.println(F("\n=== Firmware Version ==="));
    Serial.printf("FW:        %d.%d\n", FW_VERSION_MAJOR, FW_VERSION_MINOR);
    Serial.printf("HW:        %d\n", HW_VERSION);
    Serial.printf("Git:       %s%s\n", FW_GIT_HASH, FW_GIT_DIRTY ? "-dirty" : "");
    Serial.printf("Built:     %s %s\n", __DATE__, __TIME__);
    Serial.println(F("========================\n"));
}

static void print_uptime() {
    const unsigned long nowMs = millis();
    const unsigned long totalSeconds = nowMs / 1000UL;
    const unsigned long days = totalSeconds / 86400UL;
    const unsigned long hours = (totalSeconds % 86400UL) / 3600UL;
    const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    const unsigned long seconds = totalSeconds % 60UL;
    const esp_reset_reason_t reason = esp_reset_reason();

    Serial.println(F("\n=== Uptime ==="));
    Serial.printf("Uptime:       %lu ms\n", nowMs);
    Serial.printf("Human:        %lud %02luh %02lum %02lus\n",
                  days, hours, minutes, seconds);
    Serial.printf("Last reset:   %s (%d)\n", reset_reason_name(reason), (int)reason);
    Serial.println(F("==============\n"));
}

static void print_stats() {
    Serial.println(F("\n=== Runtime Stats ==="));
    Serial.printf("RX frames:         %lu\n", (unsigned long)stats.rxFrames);
    Serial.printf("RX bytes:          %lu\n", (unsigned long)stats.rxBytes);
    Serial.printf("Decode ok/fail:    %lu / %lu\n",
                  (unsigned long)stats.decodeOk,
                  (unsigned long)stats.decodeFail);
    Serial.printf("Speed updates:     %lu\n", (unsigned long)stats.speedUpdates);
    Serial.printf("ACKs sent:         %lu\n", (unsigned long)stats.ackSent);
    Serial.printf("TX frames/bytes:   %lu / %lu\n",
                  (unsigned long)stats.txFrames,
                  (unsigned long)stats.txBytes);
    Serial.printf("Connect/disconn:   %lu / %lu\n",
                  (unsigned long)stats.connectEvents,
                  (unsigned long)stats.disconnectEvents);
    Serial.printf("Advertise req:     %lu\n", (unsigned long)stats.advertiseRequests);
    Serial.printf("Button advertise:  %lu\n", (unsigned long)stats.buttonAdvertiseRequests);
    Serial.printf("CLI send:          %lu\n", (unsigned long)stats.manualCliSends);
    Serial.printf("Cmd timeouts:      %lu\n", (unsigned long)stats.cmdTimeouts);
#if TRANSPORT_BLE_ENABLED
    Serial.printf("BLE stale drops:   %lu\n", (unsigned long)stats.bleStaleDrops);
    Serial.printf("BLE queue depth:   %u\n", (unsigned)ble_rx_queue_waiting());
#endif
    Serial.println(F("=====================\n"));
}

// ---------------------------------------------------------------------------
// do_process_frame - decode a raw M25 frame and act on it.
//   Called for frames received by any active transport.
//   Returns true when the frame decoded and was acted on successfully.
// ---------------------------------------------------------------------------
static bool do_process_frame(const uint8_t* raw, size_t rawLen) {
    stats.rxFrames++;
    stats.rxBytes += (uint32_t)rawLen;

    if (cli_dbg(DBG_RAW_DATA)) {
        proto_print_hex("[RX] Raw", raw, rawLen);
    }

    // Decode: parse frame header/CRC, decrypt payload, strip padding
    uint8_t spp[64];
    size_t  sppLen = 0;
    if (!packet_decode(raw, rawLen, wheelCfg.key, spp, &sppLen)) {
        stats.decodeFail++;
        if (cli_dbg(DBG_PROTOCOL)) {
            Serial.println(F("[RX] Decode FAILED (CRC mismatch or bad crypto)"));
        }
        return false;
    }

    stats.decodeOk++;

    if (cli_dbg(DBG_RAW_DATA)) {
        proto_print_hex("[RX] SPP", spp, sppLen);
    }

    // Apply command to wheel state
    const CmdResult result = command_apply(spp, sppLen, &wheel,
                                           cli_dbg(DBG_COMMANDS));

    if (result == CMD_SPEED_UPDATE) stats.speedUpdates++;

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
    stats.txFrames++;
    stats.txBytes += (uint32_t)frameLen;

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
    stats.manualCliSends++;
    stats.ackSent++;
    uint8_t frame[128];
    const size_t frameLen = packet_encode_ack(&wheel, wheelCfg.key, frame);
    if (frameLen == 0) { Serial.println(F("[TX] ERROR: packet_encode_ack failed")); return; }
    _transport_send(frame, frameLen);
}

// ---------------------------------------------------------------------------
// do_send_response_for - context-aware reply that mirrors back service/param.
// ---------------------------------------------------------------------------
static void do_send_response_for(uint8_t reqService, uint8_t reqParam) {
    stats.ackSent++;
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
    stats.advertiseRequests++;
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
    stats.connectEvents++;
    led_set_connected();
    buzzer_beep(2);
    Serial.println(F("=== CLIENT CONNECTED ==="));

    // Reset state that should not carry over between sessions
    wheel.speed        = 0;
    wheel.lastCmdMs    = 0;
}

static void on_disconnect() {
    stats.disconnectEvents++;
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
    print_version();
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
                    stats.bleStaleDrops++;
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
    if (safety_cmd_timeout(&wheel)) {
        stats.cmdTimeouts++;
    }

    // -- Safety: button poll -----------------------------------------------
    if (safety_button_poll(do_advertise)) {
        stats.buttonAdvertiseRequests++;
    }

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
