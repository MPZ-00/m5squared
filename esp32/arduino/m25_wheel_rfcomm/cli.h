/**
 * cli.h - Serial command-line interface.
 *
 * Reads one line from Serial each call and dispatches to handler functions.
 * Knows nothing about the transport layer; receives function-pointer callbacks
 * from the main sketch for actions like "disconnect" or "send response".
 *
 * debug flags (bitfield) live here as they are only relevant to the CLI.
 *
 * Usage:
 *   CliContext ctx = cli_make_context(...)
 *   // in loop():
 *   cli_poll(&ctx, &state);
 */

#ifndef CLI_H
#define CLI_H

#include <Arduino.h>
#include "config.h"
#include "nvs_config.h"
#include "state.h"
#include "led.h"
#include "buzzer.h"
#if TRANSPORT_RFCOMM_ENABLED
#include "transport_rfcomm.h"
#endif
#if TRANSPORT_BLE_ENABLED
#include "transport_ble.h"
#endif

// ---------------------------------------------------------------------------
// Debug flags (bitfield) - toggled via the 'debug' command
// ---------------------------------------------------------------------------
#define DBG_PROTOCOL    0x01
#define DBG_CRYPTO      0x02
#define DBG_CRC         0x04
#define DBG_COMMANDS    0x08
#define DBG_RAW_DATA    0x10
#define DBG_STALE       0x20   // BLE stale-packet grace-period log

struct DebugFlagEntry {
    uint8_t     mask;
    const char* name;
    const char* desc;
};

static const DebugFlagEntry _dbgTable[] = {
    { DBG_PROTOCOL, "protocol", "Frame parsing details"   },
    { DBG_CRYPTO,   "crypto",   "Encrypt/decrypt steps"   },
    { DBG_CRC,      "crc",      "CRC check details"       },
    { DBG_COMMANDS, "commands", "Decoded command details"  },
    { DBG_RAW_DATA, "raw",      "Raw hex dumps"           },
    { DBG_STALE,    "stale",    "BLE stale-packet logging"},
};
static const uint8_t _dbgTableLen =
    (uint8_t)(sizeof(_dbgTable) / sizeof(_dbgTable[0]));

static uint8_t cliDebugFlags = 0;

// Convenience: check individual debug flags
inline bool cli_dbg(uint8_t flag) { return (cliDebugFlags & flag) != 0; }

// ---------------------------------------------------------------------------
// CliActions - callbacks provided by the main sketch
// ---------------------------------------------------------------------------
struct CliActions {
    void (*sendResponse)();    // Build and send ACK right now
    void (*disconnect)();      // Disconnect the current client
    void (*advertise)();       // (Re)start advertising
    bool (*connected)();       // Returns true if client is connected
    const uint8_t* key;        // Encryption key (for 'key' command)
    WheelRuntimeConfig* wheelConfig; // Active runtime wheel configuration
};

// ---------------------------------------------------------------------------
// _cli_print_help - list all available commands
// ---------------------------------------------------------------------------
static void _cli_print_help() {
    Serial.println(F("\n=== Available Commands ==="));
    Serial.println(F("help                   Show this help"));
    Serial.println(F("status                 Show wheel state"));
    Serial.println(F("key                    Show encryption key"));
    Serial.println(F("config [show]          Show active wheel-side config"));
    Serial.println(F("config set left|right  Persist wheel side and reboot"));
    Serial.println(F("config reset           Clear saved side and reboot"));
    Serial.println(F("hardware               Show pin assignments"));
    Serial.println(F("battery [0-100]        Get / set battery level"));
    Serial.println(F("speed <val>            Get / set simulated speed (raw units)"));
    Serial.println(F("assist [0-2]           Get / set assist level"));
    Serial.println(F("profile [0-5]          Get / set drive profile"));
    Serial.println(F("hillhold [on|off]      Get / set hill hold"));
    Serial.println(F("rotate [n]             Simulate n wheel rotations (default 1)"));
    Serial.println(F("reset                  Reset rotation counter"));
    Serial.println(F("debug [flag|all|none]  Show / toggle debug flags"));
    Serial.println(F("audio [on|off]         Toggle audio feedback"));
    Serial.println(F("visual [on|off]        Toggle visual (speed LED) feedback"));
    Serial.println(F("beep [1-10]            Play beeps"));
    Serial.println(F("tone <freq>            Play passive buzzer tone (Hz)"));
    Serial.println(F("send                   Send ACK response now"));
#if TRANSPORT_RFCOMM_ENABLED
    Serial.println(F("scn                    Show actual RFCOMM/SPP channel"));
#endif
    Serial.println(F("disconnect             Disconnect current client"));
    Serial.println(F("advertise              Restart advertising"));
    Serial.println(F("restart                Reboot ESP32"));
    Serial.println(F("power off              Enter deep sleep"));
#if TRANSPORT_BLE_ENABLED
    Serial.println(F("stale                  Show BLE stale-packet state"));
    Serial.println(F("queue                  Show BLE RX queue depth"));
#endif
    Serial.println(F("==========================\n"));
}

// ---------------------------------------------------------------------------
// _cli_print_debug_flags
// ---------------------------------------------------------------------------
static void _cli_print_debug_flags() {
    Serial.println(F("\n=== Debug Flags ==="));
    Serial.printf("Current: 0x%02X\n", cliDebugFlags);
    Serial.println(F("Flag         Status  Description"));
    Serial.println(F("------------ ------- ----------------------"));
    for (uint8_t i = 0; i < _dbgTableLen; i++) {
        const bool on = (cliDebugFlags & _dbgTable[i].mask) != 0;
        Serial.printf("%-12s [%s]  %s\n",
            _dbgTable[i].name, on ? "ON " : "off", _dbgTable[i].desc);
    }
    Serial.println(F("\ndebug <flag>  toggle  |  debug all  |  debug none\n"));
}

// ---------------------------------------------------------------------------
// _cli_print_key
// ---------------------------------------------------------------------------
static void _cli_print_key(const uint8_t* key) {
    if (!key) { Serial.println(F("[CLI] Key not set")); return; }
    Serial.print(F("Key: "));
    for (int i = 0; i < 16; i++) Serial.printf("%02X ", key[i]);
    Serial.println();
}

// ---------------------------------------------------------------------------
// _cli_print_hardware
// ---------------------------------------------------------------------------
static void _cli_print_hardware() {
    Serial.println(F("\n=== Hardware Pins ==="));
    Serial.printf("LED RED      : %d  (low battery)\n",    PIN_LED_RED);
    Serial.printf("LED YELLOW   : %d  (mid battery)\n",    PIN_LED_YELLOW);
    Serial.printf("LED GREEN    : %d  (high battery)\n",   PIN_LED_GREEN);
    Serial.printf("LED WHITE    : %d  (connection)\n",     PIN_LED_WHITE);
    Serial.printf("LED BLUE     : %d  (speed)\n",          PIN_LED_BLUE);
    Serial.printf("BUZZER ACT   : %d\n",                   PIN_BUZZER_ACTIVE);
    Serial.printf("BUZZER PASS  : %d\n",                   PIN_BUZZER_PASSIVE);
    Serial.printf("BUTTON       : %d  (active LOW)\n",     PIN_BUTTON);
    Serial.println(F("=====================\n"));
}

// ---------------------------------------------------------------------------
// _cli_handle_config
// ---------------------------------------------------------------------------
static void _cli_handle_config(const String& arg, WheelRuntimeConfig* cfg) {
    String sub = arg;
    sub.trim();
    sub.toLowerCase();

    if (sub.length() == 0 || sub == "show") {
        wheelcfg_print(cfg);
        return;
    }

    if (sub == "reset") {
        if (!wheelcfg_clear()) {
            Serial.println(F("[Config] ERROR: failed to clear NVS"));
            return;
        }

        Serial.println(F("[Config] Saved wheel side cleared; using build default after reboot."));
        delay(300);
        ESP.restart();
        return;
    }

    if (sub.startsWith("set ")) {
        String side = sub.substring(4);
        side.trim();

        uint8_t sideId;
        if (side == "left") {
            sideId = WHEEL_SIDE_LEFT;
        } else if (side == "right") {
            sideId = WHEEL_SIDE_RIGHT;
        } else {
            Serial.println(F("Usage: config set left|right"));
            return;
        }

        if (!wheelcfg_save_side(sideId)) {
            Serial.println(F("[Config] ERROR: failed to save wheel side"));
            return;
        }

        if (cfg) wheelcfg_fill_runtime(cfg, sideId, true);
        Serial.printf("[Config] Saved wheel side: %s. Rebooting to apply...\n",
                      wheelcfg_side_name(sideId));
        delay(300);
        ESP.restart();
        return;
    }

    Serial.println(F("Usage: config [show|set left|set right|reset]"));
}

// ---------------------------------------------------------------------------
// cli_poll - read one Serial line (if available) and handle it.
// ---------------------------------------------------------------------------
inline void cli_poll(const CliActions* act, WheelState* s) {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    Serial.print("> "); Serial.println(line);

    // Split into command + optional argument
    const int    sp  = line.indexOf(' ');
    String cmd = (sp > 0) ? line.substring(0, sp) : line;
    String arg = (sp > 0) ? line.substring(sp + 1) : String();
    cmd.toLowerCase();
    arg.trim();

    // -----------------------------------------------------------------------
    if (cmd == "help") {
        _cli_print_help();

    } else if (cmd == "status") {
        state_print(s);

    } else if (cmd == "key") {
        _cli_print_key(act->key);

    } else if (cmd == "config") {
        _cli_handle_config(arg, act->wheelConfig);

    } else if (cmd == "hardware") {
        _cli_print_hardware();

    } else if (cmd == "battery") {
        if (arg.length()) {
            const int v = arg.toInt();
            if (v >= 0 && v <= 100) {
                s->battery = v;
                led_show_battery(v);
                Serial.printf("Battery -> %d%%\n", v);
            } else {
                Serial.println(F("Error: 0-100"));
            }
        } else {
            Serial.printf("Battery: %d%%\n", s->battery);
        }

    } else if (cmd == "speed") {
        if (arg.length()) {
            s->speed = (int16_t)arg.toInt();
            Serial.printf("Speed -> %d\n", s->speed);
        } else {
            Serial.printf("Speed: %d raw (%.1f%%)\n", s->speed, s->speed / 2.5f);
        }

    } else if (cmd == "assist") {
        if (arg.length()) {
            const int v = arg.toInt();
            if (v >= 0 && v <= 2) {
                s->assistLevel = v;
                Serial.printf("Assist -> %d\n", v);
            } else { Serial.println(F("Error: 0-2")); }
        } else {
            Serial.printf("Assist: %d\n", s->assistLevel);
        }

    } else if (cmd == "profile") {
        if (arg.length()) {
            const int v = arg.toInt();
            if (v >= 0 && v <= 5) {
                s->driveProfile = v;
                Serial.printf("Profile -> %d\n", v);
            } else { Serial.println(F("Error: 0-5")); }
        } else {
            Serial.printf("Profile: %d\n", s->driveProfile);
        }

    } else if (cmd == "hillhold") {
        if (arg == "on" || arg == "1")       { s->hillHold = true;  Serial.println(F("Hill hold ON"));  }
        else if (arg == "off" || arg == "0") { s->hillHold = false; Serial.println(F("Hill hold OFF")); }
        else Serial.printf("Hill hold: %s\n", s->hillHold ? "ON" : "OFF");

    } else if (cmd == "rotate") {
        const int n = arg.length() ? arg.toInt() : 1;
        state_simulate_rotation(s, n);
        Serial.printf("Simulated %d rotation(s) - total: %ld\n", n, s->rotations);

    } else if (cmd == "reset") {
        s->rotations = 0; s->distance = 0.0f;
        Serial.println(F("Rotation counter reset"));

    } else if (cmd == "debug") {
        arg.toLowerCase();
        if (arg.length() == 0)      { _cli_print_debug_flags(); }
        else if (arg == "all")      { cliDebugFlags = 0xFF; Serial.println(F("All debug ON")); }
        else if (arg == "none")     { cliDebugFlags = 0x00; Serial.println(F("All debug OFF")); }
        else {
            bool found = false;
            for (uint8_t i = 0; i < _dbgTableLen; i++) {
                if (arg == _dbgTable[i].name) {
                    cliDebugFlags ^= _dbgTable[i].mask;
                    const bool on = (cliDebugFlags & _dbgTable[i].mask) != 0;
                    Serial.printf("%s -> %s\n", _dbgTable[i].name, on ? "ON" : "OFF");
                    found = true; break;
                }
            }
            if (!found) {
                Serial.printf("Unknown flag '%s'. Use: debug (no arg) to list.\n", arg.c_str());
            }
        }

    } else if (cmd == "audio") {
        if (arg == "on" || arg == "1")       { buzzer_enable();  Serial.println(F("Audio ON")); }
        else if (arg == "off" || arg == "0") { buzzer_disable(); Serial.println(F("Audio OFF")); }
        else Serial.printf("Audio: %s\n", buzzer_is_enabled() ? "ON" : "OFF");

    } else if (cmd == "visual") {
        if (arg == "on" || arg == "1")       { _ledSpeedEnabled = true;  Serial.println(F("Visual ON")); }
        else if (arg == "off" || arg == "0") { _ledSpeedEnabled = false; Serial.println(F("Visual OFF")); }
        else Serial.printf("Visual: %s\n", _ledSpeedEnabled ? "ON" : "OFF");

    } else if (cmd == "beep") {
        const uint8_t n = (uint8_t)constrain(arg.length() ? arg.toInt() : 1, 1, 10);
        buzzer_beep(n);

    } else if (cmd == "tone") {
        if (arg.length()) {
            const uint16_t freq = (uint16_t)constrain(arg.toInt(), 50, 5000);
            buzzer_tone(freq, 500);
        } else {
            Serial.println(F("Usage: tone <freq_Hz>"));
        }

    } else if (cmd == "send") {
        if (act->connected && act->connected()) {
            if (act->sendResponse) act->sendResponse();
        } else {
            Serial.println(F("Not connected"));
        }

#if TRANSPORT_RFCOMM_ENABLED
    } else if (cmd == "scn") {
        if (rfcomm_server_channel_known()) {
            const uint8_t actual = rfcomm_server_channel();
            Serial.printf("RFCOMM server channel: %u", (unsigned)actual);
            if (actual != RFCOMM_CHANNEL) {
                Serial.printf(" (config expects %d)", RFCOMM_CHANNEL);
            }
            Serial.println();
        } else {
            Serial.printf("RFCOMM server channel not available yet (config expects %d)\n",
                          RFCOMM_CHANNEL);
        }
#endif

    } else if (cmd == "disconnect") {
        if (act->connected && act->connected()) {
            if (act->disconnect) act->disconnect();
        } else {
            Serial.println(F("Not connected"));
        }

    } else if (cmd == "advertise") {
        if (act->advertise) act->advertise();
        Serial.println(F("Advertising restarted"));

    } else if (cmd == "restart") {
        Serial.println(F("Restarting..."));
        delay(300);
        ESP.restart();

    } else if (cmd == "power") {
        arg.toLowerCase();
        if (arg == "off") {
            Serial.println(F("Entering deep sleep. Press RESET to wake."));
            delay(300);
            esp_deep_sleep_start();
        } else {
            Serial.println(F("Usage: power off"));
        }

#if TRANSPORT_BLE_ENABLED
    } else if (cmd == "stale") {
        Serial.println(F("--- BLE stale-packet state ---"));
        Serial.printf("  first_valid  : %s\n", ble_first_valid() ? "yes" : "no");
        Serial.printf("  stale_count  : %u\n", ble_stale_count());
        const unsigned long ct = ble_conn_time();
        if (ct > 0) {
            Serial.printf("  conn_age     : %lums\n", millis() - ct);
        } else {
            Serial.println(F("  conn_age     : (not connected)"));
        }
        Serial.println(F("------------------------------"));

    } else if (cmd == "queue") {
        const UBaseType_t waiting = ble_rx_queue_waiting();
        Serial.printf("BLE RX queue: %u / 8 items waiting\n", (unsigned)waiting);
#endif

    } else {
        Serial.printf("Unknown command '%s'. Type 'help'.\n", cmd.c_str());
    }
}

#endif // CLI_H
