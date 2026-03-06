/*
 * serial_commands.h - Interactive serial command interface + debug output flags
 *
 * All commands are newline-terminated ASCII strings sent over UART
 * (USB-CDC, 115200 baud).  Call serialInit() in setup() and serialTick()
 * every loop iteration.
 *
 * Available commands
 * ------------------
 *   help / ?                    List all commands
 *   status                      State snapshot: system, BLE, assist, hill-hold
 *   debug                       Show all available debug flags with status
 *   debug <flag>                Toggle specific flag (js, motor, heartbeat, ble, buttons, state)
 *   debug all / off             Enable or disable all debug flags
 *   js                          One-shot joystick snapshot (raw + normalized)
 *   ble                         BLE connection status for each wheel
 *   assist <0|1|2>              Set assist level  0=indoor  1=outdoor  2=learning
 *   hillhold <on|off>           Toggle hill hold (only when motors stopped)
 *   recal                       Recalibrate joystick center position
 *   stop                        Software emergency stop (enters ERROR state)
 *   arm                         Arm motors (PAIRED -> ARMED, manual mode only)
 *   reconnect                   Trigger BLE reconnect (from ERROR or ready)
 *   power off                   Turn device off (enters deep sleep)
 *   battery                     Print battery % (requires ENABLE_BATTERY_MONITOR)
 *
 * Debug output flags (check these before printing in the main sketch)
 * -------------------------------------------------------------------
 *   DBG_JS         0x01  live joystick raw + normalized values every 200 ms
 *   DBG_MOTOR      0x02  every motor command forwarded to the wheels (20 Hz)
 *   DBG_HEARTBEAT  0x04  loop heartbeat every 5 seconds
 *   DBG_BLE        0x08  BLE connection events and errors
 *   DBG_BUTTONS    0x10  button press/release events
 *   DBG_STATE      0x20  state transition detail logging
 *
 * Adding new debug flags
 * ----------------------
 *   1. Add #define DBG_YOURFLAG 0x40 (next available bit)
 *   2. Add entry to _debugFlagTable[] array
 *   3. Use in code: if (debugFlags & DBG_YOURFLAG) Serial.println(...);
 *
 * Integration in remote_control.ino
 * ----------------------------------
 *   1.  Declare a SerialContext and fill in all pointers (after state vars).
 *   2.  Call serialInit(ctx) at the end of setup().
 *   3.  Call serialTick(ctx) at the top of loop().
 *   4.  Gate debug prints on debugFlags:
 *         if (debugFlags & DBG_MOTOR) printMotorCommand(cmd);
 */

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>
#include "device_config.h"
#include "types.h"
#include "led_control.h"
#include "joystick.h"
#include "motor_control.h"
#include "m25_ble.h"
#include <esp_chip_info.h>
#include <BLEDevice.h>
// Note: WiFi.h is NOT included here - the WiFi stack adds ~500 kB to the binary.
// Define ENABLE_WIFI in device_config.h and add #include <WiFi.h> in sketch
// if WiFi status is needed in sysinfo.

// ---------------------------------------------------------------------------
// Debug output flags - definitions in device_config.h
// ---------------------------------------------------------------------------
uint8_t debugFlags = 0;   // all off by default, accessible globally

// Debug flag metadata for better UI
struct DebugFlagInfo {
    uint8_t     mask;
    const char* name;
    const char* description;
};

static const DebugFlagInfo _debugFlagTable[] = {
    { DBG_JS,        "js",        "Joystick values (~5 Hz)" },
    { DBG_MOTOR,     "motor",     "Motor commands (20 Hz)" },
    { DBG_HEARTBEAT, "heartbeat", "Loop heartbeat (5 s)" },
    { DBG_BLE,       "ble",       "BLE events & errors" },
    { DBG_BUTTONS,   "buttons",   "Button press events" },
    { DBG_STATE,     "state",     "State transition details" },
};
static const uint8_t _debugFlagCount = sizeof(_debugFlagTable) / sizeof(_debugFlagTable[0]);

// Forward declaration
class Supervisor;

// ---------------------------------------------------------------------------
// Caller-supplied context: pointers into state in remote_control.ino
// and function pointers for state transitions (avoids extern linkage issues
// with static variables).
// ---------------------------------------------------------------------------
struct SerialContext {
    SystemState* state;
    uint8_t*     assistLevel;
    bool*        hillHoldOn;
    Supervisor*  supervisor;
    void (*fnEnterOff)();
    void (*fnRecalibrate)();
#ifdef ENABLE_BATTERY_MONITOR
    int* batteryPct;
#endif
};

// ---------------------------------------------------------------------------
// Internal: line-receive state
// ---------------------------------------------------------------------------
static char    _scBuf[128];
static uint8_t _scBufLen = 0;

// Human-readable state names (must match SystemState order)
static const char* const _stateNames[] = {
    "BOOT", "CONNECTING", "READY", "OPERATING", "ERROR", "OFF"
};

// ---------------------------------------------------------------------------
// Internal print helpers
// ---------------------------------------------------------------------------
static void _scPrintHelp() {
    Serial.println(F("--- Commands ---"));
    Serial.println(F("  help / ?                  This message"));
    Serial.println(F("  status                    System state snapshot"));
    Serial.println(F("  sysinfo                   Chip, heap, uptime, WiFi/BT status"));
    Serial.println(F("  debug                     Show all debug flags (with usage)"));
    Serial.println(F("  debug <flag>              Toggle flag (js|motor|heartbeat|ble|buttons|state)"));
    Serial.println(F("  debug all / off           Enable/disable all debug output"));
    Serial.println(F("  js                        One-shot joystick snapshot"));
    Serial.println(F("  buttons                   Debug button hardware & state"));
    Serial.println(F("  ble                       Quick BLE connection status"));
    Serial.println(F("  wheels                    Verbose per-wheel status + key"));
    Serial.println(F("  autoreconnect <on|off>    Enable/disable auto-reconnect"));
    Serial.println(F("  setmac <left|right> <MAC> Change wheel MAC (disconnects)"));
    Serial.println(F("  setkey <left|right> <hex> Change AES key (32 hex chars)"));
    Serial.println(F("  assist <0|1|2>            Set assist level"));
    Serial.println(F("                            0=indoor  1=outdoor  2=learning"));
    Serial.println(F("  hillhold <on|off>         Toggle hill hold"));
    Serial.println(F("  recal                     Recalibrate joystick center"));
    Serial.println(F("  arm                       Arm motors (PAIRED -> ARMED)"));
    Serial.println(F("  stop                      Software emergency stop"));
    Serial.println(F("  reset                     Clear ERROR state -> CONNECTING"));
    Serial.println(F("  reconnect                 Force BLE reconnect (non-ERROR states)"));
    Serial.println(F("  power off                 Turn device off (enter deep sleep)"));
    Serial.println(F("  restart                   Restart the ESP32"));
#ifdef ENABLE_BATTERY_MONITOR
    Serial.println(F("  battery                   Print battery %"));
#endif
    Serial.println(F("----------------"));
}

static void _scPrintDebugFlags() {
    Serial.println(F("--- Debug Flags ---"));
    Serial.printf("Current: 0x%02X", debugFlags);
    if (debugFlags == 0) {
        Serial.println(F("  (all disabled)"));
    } else {
        Serial.print(F("  ("));
        bool first = true;
        for (uint8_t i = 0; i < _debugFlagCount; i++) {
            if (debugFlags & _debugFlagTable[i].mask) {
                if (!first) Serial.print(F(", "));
                Serial.print(_debugFlagTable[i].name);
                first = false;
            }
        }
        Serial.println(F(")"));
    }
    Serial.println();
    Serial.println(F("Flag       Status  Description"));
    Serial.println(F("---------- ------- ----------------------------------"));
    
    for (uint8_t i = 0; i < _debugFlagCount; i++) {
        bool enabled = (debugFlags & _debugFlagTable[i].mask) != 0;
        Serial.printf("%-10s [%s]  %s\n",
            _debugFlagTable[i].name,
            enabled ? "ON " : "off",
            _debugFlagTable[i].description);
    }
    
    Serial.println(F("\nUsage:"));
    Serial.println(F("  debug              Show this list"));
    Serial.println(F("  debug <flag>       Toggle specific flag (e.g., 'debug js')"));
    Serial.println(F("  debug all          Enable all flags"));
    Serial.println(F("  debug off          Disable all flags"));
}

static void _scPrintStatus(const SerialContext &ctx) {
    uint8_t si = (uint8_t)*ctx.state;
    const char* sname = (si < 5) ? _stateNames[si] : "?";
    Serial.printf("[Status] state=%-12s  assist=%-8s  hillhold=%s\n",
        sname,
        assistConfigs[*ctx.assistLevel].name,
        *ctx.hillHoldOn ? "ON" : "off");
    Serial.printf("[Status] BLE: Left=%s  Right=%s  All=%s\n",
        bleIsConnected(WHEEL_LEFT)  ? "conn" : "disc",
        bleIsConnected(WHEEL_RIGHT) ? "conn" : "disc",
        bleAllConnected()           ? "YES"  : "no");
#ifdef ENABLE_BATTERY_MONITOR
    if (ctx.batteryPct) {
        Serial.printf("[Status] Battery: %d %%\n", *ctx.batteryPct);
    }
#endif
    
    // Compact debug flags summary
    if (debugFlags == 0) {
        Serial.println(F("[Status] Debug: off  (use 'debug' to see options)"));
    } else {
        Serial.print(F("[Status] Debug: "));
        bool first = true;
        for (uint8_t i = 0; i < _debugFlagCount; i++) {
            if (debugFlags & _debugFlagTable[i].mask) {
                if (!first) Serial.print(F(", "));
                Serial.print(_debugFlagTable[i].name);
                first = false;
            }
        }
        Serial.printf("  (0x%02X)\n", debugFlags);
    }
}

static void _scPrintBle() {
    Serial.printf("[BLE] Left  (%s): %s\n",
        LEFT_WHEEL_MAC,
        bleIsConnected(WHEEL_LEFT) ? "connected" : "disconnected");
    Serial.printf("[BLE] Right (%s): %s\n",
        RIGHT_WHEEL_MAC,
        bleIsConnected(WHEEL_RIGHT) ? "connected" : "disconnected");
    Serial.printf("[BLE] allConnected=%s  anyConnected=%s\n",
        bleAllConnected() ? "YES" : "no",
        bleAnyConnected() ? "yes" : "NO");
}

static void _scPrintJs() {
    JoystickRaw  raw = joystickReadRaw();
    JoystickNorm n   = joystickRead();
    Serial.printf("[JS] raw X=%-5d Y=%-5d  norm X=%+.3f Y=%+.3f  dz=%s\n",
        raw.x, raw.y, n.x, n.y, n.inDeadzone ? "yes" : "no");
}

static void _scPrintWheels() {
    blePrintWheelDetails();
}

static void _scPrintSysInfo() {
    // Chip
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    Serial.printf("[SYS] Chip    : %s  cores=%d  rev=%d\n",
        ESP.getChipModel(), chip.cores, chip.revision);
    Serial.printf("[SYS] CPU     : %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("[SYS] Flash   : %u kB  %s\n",
        (unsigned)(ESP.getFlashChipSize() / 1024),
        (chip.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    Serial.printf("[SYS] Heap    : free=%u B  min-free=%u B\n",
        ESP.getFreeHeap(), ESP.getMinFreeHeap());
    uint32_t up = millis() / 1000;
    Serial.printf("[SYS] Uptime  : %02u:%02u:%02u\n",
        up / 3600, (up % 3600) / 60, up % 60);
    // BT / BLE
    Serial.printf("[BT]  MAC     : %s\n",
        BLEDevice::getAddress().toString().c_str());
    Serial.printf("[BLE] autoRec : %s\n",
        bleGetAutoReconnect() ? "ON" : "off");
    // WiFi (only when ENABLE_WIFI is defined in device_config.h)
#ifdef ENABLE_WIFI
    wifi_mode_t wmode = WiFi.getMode();
    const char* modeStr =
        wmode == WIFI_MODE_NULL  ? "disabled" :
        wmode == WIFI_MODE_STA   ? "STA"      :
        wmode == WIFI_MODE_AP    ? "AP"       :
        wmode == WIFI_MODE_APSTA ? "AP+STA"   : "?";
    Serial.printf("[WiFi] mode   : %s\n", modeStr);
    if (wmode == WIFI_MODE_STA || wmode == WIFI_MODE_APSTA) {
        Serial.printf("[WiFi] SSID   : %s\n", WiFi.SSID().c_str());
        Serial.printf("[WiFi] IP     : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WiFi] RSSI   : %d dBm\n", WiFi.RSSI());
    }
#else
    Serial.println(F("[WiFi] disabled (ENABLE_WIFI not set)"));
#endif
}

// Parse exactly 32 hex characters (no spaces/colons) into 16 bytes.
// Returns true on success.
static bool _scParseHex16(const char* hex, uint8_t* out) {
    if (strlen(hex) != 32) return false;
    for (int i = 0; i < 16; i++) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        auto hexNib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = hexNib(hi), l = hexNib(lo);
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
static void _scDispatch(const char* cmd, const SerialContext &ctx) {

    // help / ?
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        _scPrintHelp();
        return;
    }

    // status
    if (strcmp(cmd, "status") == 0) {
        _scPrintStatus(ctx);
        return;
    }

    // debug (show flags or toggle specific flag)
    if (strncmp(cmd, "debug", 5) == 0) {
        const char* arg = cmd + 5;
        
        // Skip whitespace
        while (*arg == ' ') arg++;
        
        // No argument: show all flags
        if (*arg == '\0') {
            _scPrintDebugFlags();
            return;
        }
        
        // debug off
        if (strcmp(arg, "off") == 0) {
            debugFlags = 0;
            Serial.println(F("[Debug] All flags disabled"));
            _scPrintDebugFlags();
            return;
        }
        
        // debug all
        if (strcmp(arg, "all") == 0) {
            debugFlags = 0xFF;  // Enable all flags
            Serial.println(F("[Debug] All flags enabled"));
            _scPrintDebugFlags();
            return;
        }
        
        // Try to find matching flag name in table
        bool found = false;
        for (uint8_t i = 0; i < _debugFlagCount; i++) {
            if (strcmp(arg, _debugFlagTable[i].name) == 0) {
                debugFlags ^= _debugFlagTable[i].mask;  // Toggle
                bool nowEnabled = (debugFlags & _debugFlagTable[i].mask) != 0;
                Serial.printf("[Debug] %s -> %s\n", 
                    _debugFlagTable[i].name,
                    nowEnabled ? "ON" : "off");
                found = true;
                break;
            }
        }
        
        if (!found) {
            Serial.printf("[Debug] Unknown flag: '%s'\n", arg);
            Serial.println(F("[Debug] Available flags:"));
            for (uint8_t i = 0; i < _debugFlagCount; i++) {
                Serial.printf("  %-10s  %s\n", 
                    _debugFlagTable[i].name,
                    _debugFlagTable[i].description);
            }
        }
        return;
    }

    // js (one-shot snapshot)
    if (strcmp(cmd, "js") == 0) {
        _scPrintJs();
        return;
    }

    // buttons (debug hardware state)
    if (strcmp(cmd, "buttons") == 0) {
        buttonsPrintDebug();
        return;
    }

    // ble
    if (strcmp(cmd, "ble") == 0) {
        _scPrintBle();
        return;
    }

    // assist <0|1|2>
    if (strncmp(cmd, "assist ", 7) == 0) {
        int lvl = atoi(cmd + 7);
        if (lvl < 0 || lvl >= ASSIST_COUNT) {
            Serial.println(F("[CMD] assist: invalid level  0=indoor  1=outdoor  2=learning"));
            return;
        }
        if (*ctx.state == STATE_OPERATING) {
            Serial.println(F("[CMD] assist: ignored - motors active, stop first"));
            return;
        }
        *ctx.assistLevel = (uint8_t)lvl;
        ledSetAssistLevel(*ctx.assistLevel);
        bleSendAssistLevel(*ctx.assistLevel);
        Serial.printf("[CMD] Assist -> %s\n", assistConfigs[*ctx.assistLevel].name);
        return;
    }

    // hillhold <on|off>
    if (strncmp(cmd, "hillhold ", 9) == 0) {
        if (*ctx.state == STATE_OPERATING) {
            Serial.println(F("[CMD] hillhold: ignored - motors active"));
            return;
        }
        const char* arg = cmd + 9;
        if (strcmp(arg, "on") == 0) {
            *ctx.hillHoldOn = true;
        } else if (strcmp(arg, "off") == 0) {
            *ctx.hillHoldOn = false;
        } else {
            Serial.println(F("[CMD] hillhold: use 'on' or 'off'"));
            return;
        }
        ledSetHillHold(*ctx.hillHoldOn);
        bleSendHillHold(*ctx.hillHoldOn);
        Serial.printf("[CMD] HillHold -> %s\n", *ctx.hillHoldOn ? "ON" : "OFF");
        return;
    }

    // recal
    if (strcmp(cmd, "recal") == 0) {
        if (*ctx.state == STATE_OPERATING) {
            Serial.println(F("[CMD] recal: release joystick to center before recalibrating"));
            return;
        }
        Serial.println(F("[CMD] Recalibrating joystick center - keep joystick at rest..."));
        ctx.fnRecalibrate();
        return;
    }

    // arm - transition PAIRED -> ARMED
    if (strcmp(cmd, "arm") == 0) {
        if (!ctx.supervisor) {
            Serial.println(F("[CMD] ERROR: supervisor not available"));
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState != SUPERVISOR_PAIRED) {
            Serial.printf("[CMD] arm: must be in PAIRED state (currently %s)\n",
                          supervisorStateToString(supState));
            return;
        }
        ctx.supervisor->requestArm();
        Serial.println(F("[CMD] Armed - joystick + deadman to drive"));
        return;
    }

    // stop (software e-stop -> FAILSAFE state)
    if (strcmp(cmd, "stop") == 0) {
        if (ctx.supervisor) {
            Serial.println(F("[CMD] Emergency stop"));
            ctx.supervisor->requestEmergencyStop("serial stop command");
        } else {
            Serial.println(F("[CMD] ERROR: supervisor not available"));
        }
        return;
    }

    // reset - exit FAILSAFE state back to CONNECTING
    if (strcmp(cmd, "reset") == 0) {
        if (!ctx.supervisor) {
            Serial.println(F("[CMD] ERROR: supervisor not available"));
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState != SUPERVISOR_FAILSAFE) {
            Serial.println(F("[CMD] reset: not in FAILSAFE state"));
            return;
        }
        Serial.println(F("[CMD] Clearing failsafe, reconnecting..."));
        ctx.supervisor->requestReconnect();
        return;
    }

    // reconnect - force BLE reconnect
    if (strcmp(cmd, "reconnect") == 0) {
        if (!ctx.supervisor) {
            Serial.println(F("[CMD] ERROR: supervisor not available"));
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState == SUPERVISOR_DRIVING) {
            Serial.println(F("[CMD] reconnect: stop motors first"));
            return;
        }
        if (*ctx.state == STATE_OFF) {
            Serial.println(F("[CMD] reconnect: device is OFF, use 'power on' first"));
            return;
        }
        Serial.println(F("[CMD] Forcing reconnect..."));
        ctx.supervisor->requestReconnect();
        return;
    }

    // power <on|off>
    if (strncmp(cmd, "power ", 6) == 0) {
        const char* arg = cmd + 6;
        if (strcmp(arg, "off") == 0) {
            if (*ctx.state == STATE_OFF) {
                Serial.println(F("[CMD] power: already off"));
                return;
            }
            Serial.println(F("[CMD] Turning OFF (entering deep sleep)..."));
            ctx.fnEnterOff();  // Never returns - enters deep sleep
        } else if (strcmp(arg, "on") == 0) {
            // Deep sleep means device reboots on wake, so this command only
            // makes sense during development/testing when deep sleep is disabled
            if (*ctx.state != STATE_OFF) {
                Serial.println(F("[CMD] power: already on"));
                return;
            }
            Serial.println(F("[CMD] Note: Device uses deep sleep. Power button causes reboot."));
            Serial.println(F("[CMD] This command only works if deep sleep is disabled."));
        } else {
            Serial.println(F("[CMD] power: use 'on' or 'off'"));
        }
        return;
    }

#ifdef ENABLE_BATTERY_MONITOR
    // battery
    if (strcmp(cmd, "battery") == 0) {
        if (ctx.batteryPct) {
            Serial.printf("[Battery] %d %%\n", *ctx.batteryPct);
        }
        return;
    }
#endif

    // wheels - verbose per-wheel dump
    if (strcmp(cmd, "wheels") == 0) {
        _scPrintWheels();
        return;
    }

    // sysinfo
    if (strcmp(cmd, "sysinfo") == 0) {
        _scPrintSysInfo();
        return;
    }

    // autoreconnect <on|off>
    if (strncmp(cmd, "autoreconnect ", 14) == 0) {
        const char* arg = cmd + 14;
        if (strcmp(arg, "on") == 0) {
            bleSetAutoReconnect(true);
        } else if (strcmp(arg, "off") == 0) {
            bleSetAutoReconnect(false);
        } else {
            Serial.println(F("[CMD] autoreconnect: use 'on' or 'off'"));
        }
        return;
    }

    // setmac <left|right> <XX:XX:XX:XX:XX:XX>
    if (strncmp(cmd, "setmac ", 7) == 0) {
        const char* rest = cmd + 7;
        int idx = -1;
        if (strncmp(rest, "left ",  5) == 0) { idx = WHEEL_LEFT;  rest += 5; }
        else if (strncmp(rest, "right ", 6) == 0) { idx = WHEEL_RIGHT; rest += 6; }
        if (idx < 0) {
            Serial.println(F("[CMD] setmac: setmac left <MAC>  or  setmac right <MAC>"));
            return;
        }
        if (strlen(rest) != 17) {
            Serial.println(F("[CMD] setmac: MAC must be XX:XX:XX:XX:XX:XX (17 chars)"));
            return;
        }
        if (*ctx.state == STATE_OPERATING) {
            Serial.println(F("[CMD] setmac: stop motors first"));
            return;
        }
        bleSetMac(idx, rest);
        return;
    }

    // setkey <left|right> <32 hex chars, no spaces>
    if (strncmp(cmd, "setkey ", 7) == 0) {
        const char* rest = cmd + 7;
        int idx = -1;
        if (strncmp(rest, "left ",  5) == 0) { idx = WHEEL_LEFT;  rest += 5; }
        else if (strncmp(rest, "right ", 6) == 0) { idx = WHEEL_RIGHT; rest += 6; }
        if (idx < 0) {
            Serial.println(F("[CMD] setkey: setkey left <32hex>  or  setkey right <32hex>"));
            return;
        }
        uint8_t newKey[16];
        if (!_scParseHex16(rest, newKey)) {
            Serial.println(F("[CMD] setkey: key must be exactly 32 hex chars, no spaces/colons"));
            return;
        }
        bleSetKey(idx, newKey);
        return;
    }

    // restart
    if (strcmp(cmd, "restart") == 0) {
        Serial.println(F("[CMD] Restarting ESP32..."));
        delay(200);
        ESP.restart();
        return;
    }

    Serial.printf("[CMD] Unknown: '%s'  (type 'help')\n", cmd);
}

// ---------------------------------------------------------------------------
// Live joystick output timer
// ---------------------------------------------------------------------------
static uint32_t _scLastJsMs = 0;
#define SC_JS_INTERVAL_MS 200   // ~5 Hz

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/*  Call once at the end of setup().  Prints the ready banner. */
inline void serialInit(const SerialContext &ctx) {
    (void)ctx;
    Serial.println(F("[Serial] Ready - type 'help' for commands"));
}

/*  Call every loop() iteration.  Handles input parsing and live output. */
inline void serialTick(const SerialContext &ctx) {

    // --- Accumulate incoming characters into a line buffer ---
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;   // discard CR  (handles CRLF line endings)
        if (c == '\n') {
            if (_scBufLen > 0) {
                _scBuf[_scBufLen] = '\0';

                // Trim leading whitespace
                char* start = _scBuf;
                while (*start == ' ') start++;

                // Trim trailing whitespace
                char* end = start + (int)strlen(start) - 1;
                while (end > start && (*end == ' ' || *end == '\r')) {
                    *end-- = '\0';
                }

                if (*start != '\0') {
                    Serial.printf("> %s\n", start);   // echo
                    _scDispatch(start, ctx);
                }
                _scBufLen = 0;
            }
        } else {
            if (_scBufLen < (uint8_t)(sizeof(_scBuf) - 1)) {
                _scBuf[_scBufLen++] = c;
            }
            // overflow: silently drop characters beyond the buffer
        }
    }

    // --- Live joystick stream (DBG_JS) ---
    if ((debugFlags & DBG_JS) &&
        (millis() - _scLastJsMs >= SC_JS_INTERVAL_MS)) {
        _scLastJsMs = millis();
        _scPrintJs();
    }
}

#endif // SERIAL_COMMANDS_H
