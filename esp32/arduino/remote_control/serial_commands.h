/*
 * serial_commands.h - Interactive serial command interface + logger controls
 *
 * All commands are newline-terminated ASCII strings sent over UART
 * (USB-CDC, 115200 baud).  Call serialInit() in setup() and serialTick()
 * every loop iteration.
 *
 * Available commands
 * ------------------
 *   help / ?                    List all commands
 *   status                      Full status: state, wheels, telemetry, watchdogs, record
 *   log                         Show logger level + tag status
 *   log level <...>             Set logger level (none/error/warn/info/debug/verbose)
 *   log tag <name> <on|off>     Enable/disable one logger tag
 *   log all <on|off>            Enable/disable all logger tags
 *   debug ...                   Compatibility alias to log controls
 *   js                          One-shot joystick snapshot (raw + normalized)
 *   ble                         BLE connection status for each wheel
 *   wheels                      Verbose per-wheel status + key
 *   telemetry                   Request fresh telemetry from wheels + print cached values
 *   assist <0|1|2>              Set assist level  0=indoor  1=outdoor  2=learning
 *   hillhold <on|off>           Toggle hill hold (only when motors stopped)
 *   recal                       Recalibrate joystick center position
 *   arm                         Arm motors (PAIRED -> ARMED, manual mode only)
 *   disarm                      Disarm motors (ARMED/DRIVING -> PAIRED, safe stop first)
 *   stop                        Software emergency stop (enters FAILSAFE state)
 *   reset                       Clear FAILSAFE state -> reconnect
 *   reconnect                   Trigger BLE reconnect
 *   disconnect                  Force-disconnect all wheels
 *   record start [N]            Record BLE traffic for N seconds (default 10), auto-dumps on expiry
 *   record stop                 Stop recording early
 *   record dump                 Print captured traffic log
 *   autoreconnect <on|off>      Enable/disable auto-reconnect
 *   power off                   Turn device off (enters deep sleep)
 *   battery                     Print battery % (requires ENABLE_BATTERY_MONITOR)
 *
 * Logger tag controls
 * -------------------
 *   joystick, motor, heartbeat, ble, button, state, telemetry, tx, auth
 *
 * Integration in remote_control.ino
 * ----------------------------------
 *   1.  Declare a SerialContext and fill in all pointers (after state vars).
 *   2.  Call serialInit(ctx) at the end of setup().
 *   3.  Call serialTick(ctx) at the top of loop().
 *   4.  Prefer logger tags for diagnostics:
 *         log tag motor on
 */

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>
#include "device_config.h"
#include "nvs_config.h"
#include "types.h"
#include "led_control.h"
#include "joystick.h"
#include "motor_control.h"
#include "m25_ble.h"
#include "Logger.h"
#include <esp_chip_info.h>
#include <BLEDevice.h>
#include <stdarg.h>
 // Note: WiFi.h is NOT included here - the WiFi stack adds ~500 kB to the binary.
 // Define ENABLE_WIFI in device_config.h and add #include <WiFi.h> in sketch
 // if WiFi status is needed in sysinfo.

#ifndef ENV_PROFILE_AVAILABLE
#define ENV_PROFILE_AVAILABLE 0
#endif

#ifndef ENV_LEFT_WHEEL_MAC
#define ENV_LEFT_WHEEL_MAC LEFT_WHEEL_MAC
#endif

#ifndef ENV_RIGHT_WHEEL_MAC
#define ENV_RIGHT_WHEEL_MAC RIGHT_WHEEL_MAC
#endif

#ifndef ENV_ENCRYPTION_KEY_LEFT
#define ENV_ENCRYPTION_KEY_LEFT ENCRYPTION_KEY_LEFT
#endif

#ifndef ENV_ENCRYPTION_KEY_RIGHT
#define ENV_ENCRYPTION_KEY_RIGHT ENCRYPTION_KEY_RIGHT
#endif

struct LogTagInfo {
    uint32_t    tag;
    const char* name;
    const char* description;
};

static const LogTagInfo _logTagTable[] = {
    { TAG_JOYSTICK,   "joystick",  "Joystick values (~5 Hz)" },
    { TAG_MOTOR,      "motor",     "Motor commands (20 Hz)" },
    { TAG_SUPERVISOR, "heartbeat", "Loop/supervisor heartbeat" },
    { TAG_BLE,        "ble",       "BLE events & errors" },
    { TAG_BUTTON,     "button",    "Button press events" },
    { TAG_SUPERVISOR, "state",     "State transition details" },
    { TAG_TELEMETRY,  "telemetry", "BLE telemetry responses" },
    { TAG_TX,         "tx",        "Raw BLE TX/RX hex dumps" },
    { TAG_AUTH,       "auth",      "RFCOMM auth/pairing and SPP events" },
};
static const uint8_t _logTagCount = sizeof(_logTagTable) / sizeof(_logTagTable[0]);

static bool _scTryGetTagByName(const char* name, uint32_t* outTag) {
    if (!name || !outTag) return false;
    for (uint8_t i = 0; i < _logTagCount; i++) {
        if (strcmp(name, _logTagTable[i].name) == 0) {
            *outTag = _logTagTable[i].tag;
            return true;
        }
    }
    return false;
}

static const char* _scLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::NONE: return "none";
    case LogLevel::ERROR: return "error";
    case LogLevel::WARN: return "warn";
    case LogLevel::INFO: return "info";
    case LogLevel::DEBUG: return "debug";
    case LogLevel::VERBOSE: return "verbose";
    default: return "?";
    }
}

static bool _scTryParseLevel(const char* text, LogLevel* outLevel) {
    if (!text || !outLevel) return false;
    if (strcmp(text, "none") == 0) { *outLevel = LogLevel::NONE; return true; }
    if (strcmp(text, "error") == 0) { *outLevel = LogLevel::ERROR; return true; }
    if (strcmp(text, "warn") == 0) { *outLevel = LogLevel::WARN; return true; }
    if (strcmp(text, "info") == 0) { *outLevel = LogLevel::INFO; return true; }
    if (strcmp(text, "debug") == 0) { *outLevel = LogLevel::DEBUG; return true; }
    if (strcmp(text, "verbose") == 0) { *outLevel = LogLevel::VERBOSE; return true; }
    return false;
}

// Forward declaration
class Supervisor;

// ---------------------------------------------------------------------------
// Caller-supplied context: pointers into state in remote_control.ino
// and function pointers for state transitions (avoids extern linkage issues
// with static variables).
// ---------------------------------------------------------------------------
struct SerialContext {
    SystemState* state;
    uint8_t* assistLevel;
    bool* hillHoldOn;
    Supervisor* supervisor;
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

static const bool _scProfileEnvAvailable = (ENV_PROFILE_AVAILABLE != 0);
static const char _scProfileEnvLeftMac[] = ENV_LEFT_WHEEL_MAC;
static const char _scProfileEnvRightMac[] = ENV_RIGHT_WHEEL_MAC;
static const uint8_t _scProfileEnvLeftKey[16] = ENV_ENCRYPTION_KEY_LEFT;
static const uint8_t _scProfileEnvRightKey[16] = ENV_ENCRYPTION_KEY_RIGHT;

// Default profile: the build-time values from device_config.h (unchanged by load_env.py)
static const char _scProfileDefaultLeftMac[] = LEFT_WHEEL_MAC;
static const char _scProfileDefaultRightMac[] = RIGHT_WHEEL_MAC;
static const uint8_t _scProfileDefaultLeftKey[16] = ENCRYPTION_KEY_LEFT;
static const uint8_t _scProfileDefaultRightKey[16] = ENCRYPTION_KEY_RIGHT;

static void _scCmdOut(const char* msg);
static void _scCmdOutf(const char* fmt, ...);

// ---------------------------------------------------------------------------
// Internal print helpers
// ---------------------------------------------------------------------------
static void _scPrintHelp() {
    _scCmdOut("--- Commands ---");
    _scCmdOut("  help / ?                  This message");
    _scCmdOut("  status                    Full system status (state, wheels, telemetry, watchdogs)");
    _scCmdOut("  sysinfo                   Chip, heap, uptime, WiFi/BT status");
    _scCmdOut("--- Logging ---");
    _scCmdOut("  log                       Show logger level and tags");
    _scCmdOut("  log level <none|error|warn|info|debug|verbose>");
    _scCmdOut("  log tag <name> <on|off>   names: joystick|motor|heartbeat|ble|button|state|telemetry|tx|auth");
    _scCmdOut("  log all <on|off>          Enable/disable all tags");
    _scCmdOut("  debug ...                 Compatibility alias for existing scripts");
    _scCmdOut("  txstats [reset]           Show/reset BLE TX command counters");
    _scCmdOut("  log stop <on|off>         Enable/disable STOP lines in motor logs");
    _scCmdOut("  log stop every <N>        Print every Nth STOP line (when enabled)");
    _scCmdOut("  log show                  Show current log-filter settings");
    _scCmdOut("  js                        One-shot joystick snapshot");
    _scCmdOut("  buttons                   Button hardware and state details");
    _scCmdOut("  ble                       Quick BLE connection status");
    _scCmdOut("  wheels                    Verbose per-wheel status + key");
    _scCmdOut("  telemetry                 Request fresh telemetry from wheels + print cached values");
    _scCmdOut("--- Recording ---");
    _scCmdOut("  record start [N]          Record BLE traffic for N seconds (default 10)");
    _scCmdOut("  record stop               Stop recording early");
    _scCmdOut("  record dump               Print captured traffic log");
    _scCmdOut("--- Connection ---");
    _scCmdOut("  autoreconnect <on|off>    Enable/disable auto-reconnect");
    _scCmdOut("  disconnect                Force-disconnect all wheels");
    _scCmdOut("  reconnect                 Force BLE reconnect");
    _scCmdOut("  setmac <left|right> <MAC> Change wheel MAC (disconnects)");
    _scCmdOut("  setkey <left|right> <hex> Change AES key (32 hex chars)");
    _scCmdOut("--- Control ---");
    _scCmdOut("  assist <0|1|2>            Set assist level  0=indoor  1=outdoor  2=learning");
    _scCmdOut("  hillhold <on|off>         Toggle hill hold");
    _scCmdOut("  recal                     Recalibrate joystick center");
    _scCmdOut("  arm                       Arm motors (PAIRED -> ARMED)");
    _scCmdOut("  disarm                    Disarm motors (ARMED/DRIVING -> PAIRED)");
    _scCmdOut("  stop                      Software emergency stop (-> FAILSAFE)");
    _scCmdOut("  reset                     Clear FAILSAFE state -> reconnect");
    _scCmdOut("--- Config (NVS) ---");
    _scCmdOut("  config show               Print MACs and keys (NVS vs build default)");
    _scCmdOut("  config reset              Clear NVS; build defaults on next boot");
    _scCmdOut("  config profile <env|default> Persist+apply profile now, then reconnect");
    _scCmdOut("                               env requires build-time .env values");
    _scCmdOut("--- System ---");
    _scCmdOut("  power off                 Turn device off (enter deep sleep)");
    _scCmdOut("  restart                   Restart the ESP32");
#ifdef ENABLE_BATTERY_MONITOR
    _scCmdOut("  battery                   Print battery %");
#endif
    _scCmdOut("----------------");
}

static void _scPrintLoggerSettings() {
    Logger& logger = Logger::instance();
    uint32_t tagMask = logger.getTagMask();

    _scCmdOut("--- Logger Settings ---");
    _scCmdOutf("Level: %s", _scLevelName(logger.getLevel()));
    _scCmdOutf("Tag mask: 0x%08lX", (unsigned long)tagMask);
    _scCmdOut("Tag         Status  Description");
    _scCmdOut("----------  ------  ----------------------------------");
    for (uint8_t i = 0; i < _logTagCount; i++) {
        bool enabled = (tagMask & _logTagTable[i].tag) != 0;
        _scCmdOutf("%-10s  %-6s  %s",
            _logTagTable[i].name,
            enabled ? "ON" : "off",
            _logTagTable[i].description);
    }
}

static void _scPrintStatus(const SerialContext& ctx) {
    _scCmdOut("=== Remote Control Status ===");

    // --- Supervisor state ---
    if (ctx.supervisor) {
        SupervisorState supState = ctx.supervisor->getState();
        const char* stateName = supervisorStateToString(supState);
        char stateLine[160];
        snprintf(stateLine, sizeof(stateLine), "[State]    %s", stateName);
        if (supState == SUPERVISOR_ARMED) {
            uint32_t remMs = ctx.supervisor->getArmedIdleRemainingMs();
            uint32_t armMs = ctx.supervisor->getConfig().armIdleTimeoutMs;
            uint32_t usedMs = armMs - remMs;
            char suffix[96];
            snprintf(suffix, sizeof(suffix), "  (idle %.1f s / %.0f s before auto-disarm)",
                usedMs / 1000.0f, armMs / 1000.0f);
            strlcat(stateLine, suffix, sizeof(stateLine));
        }
        _scCmdOut(stateLine);

        // Reconnect info
        uint8_t retries = ctx.supervisor->getReconnectAttempts();
        uint8_t maxRet = ctx.supervisor->getConfig().maxReconnectAttempts;
        if (retries > 0 || supState == SUPERVISOR_CONNECTING) {
            _scCmdOutf("[Reconnect] %d / %d attempts", retries, maxRet);
        }
    }
    else {
        uint8_t si = (uint8_t)*ctx.state;
        const char* sname = (si < 6) ? _stateNames[si] : "?";
        _scCmdOutf("[State]    %s", sname);
    }

    // --- BLE connection ---
    bool leftConn = bleIsConnected(WHEEL_LEFT);
    bool rightConn = bleIsConnected(WHEEL_RIGHT);
    _scCmdOutf("[BLE]      Left=%-5s  Right=%-5s  autoReconn=%s",
        leftConn ? "OK" : "disc",
        rightConn ? "OK" : "disc",
        bleGetAutoReconnect() ? "ON" : "off");

    // --- Per-wheel telemetry ---
    {
        auto printWheel = [](const char* label, int idx, bool conn) {
            char line[192];
            snprintf(line, sizeof(line), "[Wheel %s]", label);
            if (!conn) {
                strlcat(line, "  not connected", sizeof(line));
                _scCmdOut(line);
                return;
            }
            int8_t batt = bleGetBattery(idx);
            if (batt >= 0) {
                char seg[32];
                snprintf(seg, sizeof(seg), "  battery=%d%%", batt);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "  battery=--", sizeof(line));
            }
            uint8_t maj = 0, min = 0, patch = 0;
            if (bleGetFirmwareVersion(idx, maj, min, patch)) {
                char seg[32];
                snprintf(seg, sizeof(seg), "  FW=%d.%d.%d", maj, min, patch);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "  FW=--", sizeof(line));
            }
            float dist = bleGetDistanceKm(idx);
            if (dist >= 0.0f) {
                char seg[32];
                snprintf(seg, sizeof(seg), "  dist=%.2f km", dist);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "  dist=--", sizeof(line));
            }
            _scCmdOut(line);
            };
        printWheel("Left ", WHEEL_LEFT, leftConn);
        printWheel("Right", WHEEL_RIGHT, rightConn);
    }

    // --- Watchdog timers (only meaningful when armed/driving) ---
    if (ctx.supervisor) {
        SupervisorState supState = ctx.supervisor->getState();
        if (supState == SUPERVISOR_ARMED || supState == SUPERVISOR_DRIVING) {
            const SupervisorConfig& cfg = ctx.supervisor->getConfig();
            uint32_t sinceInput = ctx.supervisor->getTimeSinceLastInput();
            uint32_t sinceLink = ctx.supervisor->getTimeSinceLastLink();
            _scCmdOutf("[Watchdog] input=%.2f s (limit %.1f s)  link=%.2f s (limit %.1f s)%s",
                sinceInput / 1000.0f, cfg.inputTimeoutMs / 1000.0f,
                sinceLink / 1000.0f, cfg.linkTimeoutMs / 1000.0f,
                ctx.supervisor->isInputTimeout() || ctx.supervisor->isLinkTimeout() ? "  *** TIMEOUT ***" : "");
        }
    }

    // --- Assist / hill hold ---
    _scCmdOutf("[Assist]   %s  HillHold=%s",
        assistConfigs[*ctx.assistLevel].name,
        *ctx.hillHoldOn ? "ON" : "off");

#ifdef ENABLE_BATTERY_MONITOR
    if (ctx.batteryPct) {
        _scCmdOutf("[Battery]  %d %%", *ctx.batteryPct);
    }
#endif

    // --- Logger status ---
    Logger& logger = Logger::instance();
    uint32_t tagMask = logger.getTagMask();
    _scCmdOutf("[Log]      level=%s  tags=0x%08lX",
        _scLevelName(logger.getLevel()),
        (unsigned long)tagMask);

    // --- Record status ---
    if (bleRecordIsActive()) {
        _scCmdOutf("[Record]   ACTIVE  (%d entries so far)",
            (int)bleRecordEntryCount());
    }
    else {
        uint32_t cnt = bleRecordEntryCount();
        if (cnt > 0) {
            _scCmdOutf("[Record]   idle  (last capture: %d entries - use 'record dump')", (int)cnt);
        }
        else {
            _scCmdOut("[Record]   idle  (use 'record start [s]' to capture)");
        }
    }

    _scCmdOut("=============================");
}

static void _scPrintBle() {
    _scCmdOutf("[BLE] Left  (%s): %s",
        LEFT_WHEEL_MAC,
        bleIsConnected(WHEEL_LEFT) ? "connected" : "disconnected");
    _scCmdOutf("[BLE] Right (%s): %s",
        RIGHT_WHEEL_MAC,
        bleIsConnected(WHEEL_RIGHT) ? "connected" : "disconnected");
    _scCmdOutf("[BLE] allConnected=%s  anyConnected=%s",
        bleAllConnected() ? "YES" : "no",
        bleAnyConnected() ? "yes" : "NO");
}

static void _scPrintJs() {
    // Use a single ADC read so raw and norm values are consistent.
    // Previously two separate reads were made (joystickReadRaw() + joystickRead())
    // which could sample different ADC values and produce misleading log output.
    JoystickRaw  raw = joystickReadRaw();
    JoystickNorm n;
    n.x = joystickNormalizeAxis(raw.x, _jsXCenter);
    n.y = joystickNormalizeAxis(raw.y, _jsYCenter);
    n.inDeadzone = (n.x == 0.0f && n.y == 0.0f);
    _scCmdOutf("[JS] raw X=%-5d Y=%-5d  ctr X=%-5d Y=%-5d  norm X=%+.3f Y=%+.3f  dz=%s",
        raw.x, raw.y, _jsXCenter, _jsYCenter, n.x, n.y, n.inDeadzone ? "yes" : "no");
}

static void _scPrintWheels() {
    blePrintWheelDetails();
}

static void _scPrintSysInfo() {
    // Chip
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    _scCmdOutf("[SYS] Chip    : %s  cores=%d  rev=%d",
        ESP.getChipModel(), chip.cores, chip.revision);
    _scCmdOutf("[SYS] CPU     : %d MHz", ESP.getCpuFreqMHz());
    _scCmdOutf("[SYS] Flash   : %u kB  %s",
        (unsigned)(ESP.getFlashChipSize() / 1024),
        (chip.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    _scCmdOutf("[SYS] Heap    : free=%u B  min-free=%u B",
        ESP.getFreeHeap(), ESP.getMinFreeHeap());
    uint32_t up = millis() / 1000;
    _scCmdOutf("[SYS] Uptime  : %02u:%02u:%02u",
        up / 3600, (up % 3600) / 60, up % 60);
    // BT / BLE
    _scCmdOutf("[BT]  MAC     : %s",
        BLEDevice::getAddress().toString().c_str());
    _scCmdOutf("[BLE] autoRec : %s",
        bleGetAutoReconnect() ? "ON" : "off");
    // WiFi (only when ENABLE_WIFI is defined in device_config.h)
#ifdef ENABLE_WIFI
    wifi_mode_t wmode = WiFi.getMode();
    const char* modeStr =
        wmode == WIFI_MODE_NULL ? "disabled" :
        wmode == WIFI_MODE_STA ? "STA" :
        wmode == WIFI_MODE_AP ? "AP" :
        wmode == WIFI_MODE_APSTA ? "AP+STA" : "?";
    _scCmdOutf("[WiFi] mode   : %s", modeStr);
    if (wmode == WIFI_MODE_STA || wmode == WIFI_MODE_APSTA) {
        _scCmdOutf("[WiFi] SSID   : %s", WiFi.SSID().c_str());
        _scCmdOutf("[WiFi] IP     : %s", WiFi.localIP().toString().c_str());
        _scCmdOutf("[WiFi] RSSI   : %d dBm", WiFi.RSSI());
    }
#else
    _scCmdOut("[WiFi] disabled (ENABLE_WIFI not set)");
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

static void _scApplyProfile(const char* lmac, const char* rmac,
    const uint8_t* lkey, const uint8_t* rkey,
    const SerialContext& ctx) {
    bleSetMac(WHEEL_LEFT, lmac);
    bleSetMac(WHEEL_RIGHT, rmac);
    bleSetKey(WHEEL_LEFT, lkey);
    bleSetKey(WHEEL_RIGHT, rkey);

    if (ctx.supervisor) {
        ctx.supervisor->requestReconnect();
    }
}

// Command feedback has priority: always print to serial, and mirror with a prefix-specific tag.
static uint32_t _scCmdTagFromMsg(const char* msg) {
    if (!msg || msg[0] != '[') return TAG_CMD;

    if (strncmp(msg, "[Config]", 8) == 0) return TAG_CONFIG;
    if (strncmp(msg, "[TX]", 4) == 0) return TAG_TX;
    if (strncmp(msg, "[Record]", 8) == 0) return TAG_RECORD;
    if (strncmp(msg, "[Telemetry]", 11) == 0) return TAG_TELEMETRY;
    if (strncmp(msg, "[Battery]", 9) == 0) return TAG_TELEMETRY;
    if (strncmp(msg, "[Sys]", 5) == 0 || strncmp(msg, "[WiFi]", 6) == 0) return TAG_SYS;
    if (strncmp(msg, "[Log]", 5) == 0 || strncmp(msg, "[Serial]", 8) == 0) return TAG_CMD;
    if (strncmp(msg, "[CMD]", 5) == 0) return TAG_CMD;

    return TAG_CMD;
}

static void _scCmdDecorateMsg(const char* msg, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (!msg) {
        out[0] = '\0';
        return;
    }

    if (strncmp(msg, "[CMD]", 5) == 0) {
        snprintf(out, outSize, "%s", msg);
        return;
    }

    if (msg[0] == '[') {
        snprintf(out, outSize, "\b%s", msg);
        return;
    }

    snprintf(out, outSize, "%s", msg);
}

static void _scCmdOut(const char* msg) {
    if (!msg) return;
    char outBuf[320];
    _scCmdDecorateMsg(msg, outBuf, sizeof(outBuf));
    Logger::instance().logForced(LogLevel::INFO, _scCmdTagFromMsg(msg), __FILE__, __LINE__, "%s", outBuf);
}

static void _scCmdOutf(const char* fmt, ...) {
    if (!fmt) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    char outBuf[320];
    _scCmdDecorateMsg(buf, outBuf, sizeof(outBuf));
    Logger::instance().logForced(LogLevel::INFO, _scCmdTagFromMsg(buf), __FILE__, __LINE__, "%s", outBuf);
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
static void _scDispatch(const char* cmd, const SerialContext& ctx) {

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

    // log (new logger-native control plane)
    if (strncmp(cmd, "log", 3) == 0) {
        const char* arg = cmd + 3;
        while (*arg == ' ') arg++;

        if (*arg == '\0' || strcmp(arg, "show") == 0) {
            _scPrintLoggerSettings();
            return;
        }

        if (strncmp(arg, "level ", 6) == 0) {
            LogLevel lvl;
            if (!_scTryParseLevel(arg + 6, &lvl)) {
                _scCmdOut("[Log] level: use none|error|warn|info|debug|verbose");
                return;
            }
            Logger::instance().setLevel(lvl, true);
            _scCmdOutf("[Log] level -> %s", _scLevelName(lvl));
            return;
        }

        if (strncmp(arg, "all ", 4) == 0) {
            const char* mode = arg + 4;
            if (strcmp(mode, "on") == 0) {
                Logger::instance().setTagMask(TAG_ALL, true);
            }
            else if (strcmp(mode, "off") == 0) {
                Logger::instance().setTagMask(0, true);
            }
            else {
                _scCmdOut("[Log] all: use 'on' or 'off'");
                return;
            }
            _scPrintLoggerSettings();
            return;
        }

        if (strncmp(arg, "tag ", 4) == 0) {
            const char* p = arg + 4;
            char tagName[16] = { 0 };
            size_t n = 0;
            while (*p != '\0' && *p != ' ' && n < sizeof(tagName) - 1) {
                tagName[n++] = *p++;
            }
            while (*p == ' ') p++;

            uint32_t tag = 0;
            if (!_scTryGetTagByName(tagName, &tag)) {
                _scCmdOutf("[Log] unknown tag: '%s'", tagName);
                return;
            }
            if (strcmp(p, "on") == 0) {
                Logger::instance().setTagEnabled(tag, true, true);
            }
            else if (strcmp(p, "off") == 0) {
                Logger::instance().setTagEnabled(tag, false, true);
            }
            else {
                _scCmdOut("[Log] tag: use 'log tag <name> <on|off>'");
                return;
            }
            _scCmdOutf("[Log] tag %s -> %s", tagName, p);
            return;
        }

        _scCmdOut("[Log] usage: 'log', 'log level <...>', 'log tag <name> <on|off>', 'log all <on|off>'");
        return;
    }

    // debug alias (compatibility)
    if (strncmp(cmd, "debug", 5) == 0) {
        const char* arg = cmd + 5;
        while (*arg == ' ') arg++;

        if (*arg == '\0') {
            _scPrintLoggerSettings();
            return;
        }
        if (strcmp(arg, "off") == 0) {
            Logger::instance().setTagMask(0, true);
            _scCmdOut("[Log] alias 'debug off' applied (preferred: 'log all off')");
            return;
        }
        if (strcmp(arg, "all") == 0) {
            Logger::instance().setTagMask(TAG_ALL, true);
            _scCmdOut("[Log] alias 'debug all' applied (preferred: 'log all on')");
            return;
        }

        uint32_t tag = 0;
        if (!_scTryGetTagByName(arg, &tag)) {
            // compatibility alias map
            if (strcmp(arg, "js") == 0) tag = TAG_JOYSTICK;
            else if (strcmp(arg, "buttons") == 0) tag = TAG_BUTTON;
            else if (strcmp(arg, "proto") == 0) tag = TAG_TX;
            else if (strcmp(arg, "telemetry") == 0) tag = TAG_TELEMETRY;
            else if (strcmp(arg, "ble") == 0) tag = TAG_BLE;
            else if (strcmp(arg, "state") == 0 || strcmp(arg, "heartbeat") == 0) tag = TAG_SUPERVISOR;
            else if (strcmp(arg, "motor") == 0) tag = TAG_MOTOR;
            else if (strcmp(arg, "auth") == 0) tag = TAG_AUTH;
        }

        if (tag == 0) {
            _scCmdOutf("[Log] unknown alias tag: '%s'", arg);
            _scCmdOut("[Log] Use 'log' to list supported tag names.");
            return;
        }

        bool currentlyEnabled = Logger::instance().isTagEnabled(tag);
        Logger::instance().setTagEnabled(tag, !currentlyEnabled, true);
        _scCmdOut("[Log] alias toggled tag (preferred: 'log tag <name> <on|off>')");
        return;
    }

    // txstats [reset]
    if (strcmp(cmd, "txstats") == 0) {
        blePrintTxStats();
        return;
    }
    if (strcmp(cmd, "txstats reset") == 0) {
        bleResetTxStats();
        _scCmdOut("[TX] Counters reset");
        return;
    }

    // log show | log stop <on|off> | log stop every <N>
    if (strcmp(cmd, "log show") == 0) {
        _scCmdOutf("[Log] stop=%s every=%u",
            bleGetMotorStopLogEnabled() ? "ON" : "off",
            (unsigned)bleGetMotorStopLogEvery());
        return;
    }
    if (strncmp(cmd, "log stop ", 9) == 0) {
        const char* arg = cmd + 9;
        if (strcmp(arg, "on") == 0) {
            bleSetMotorStopLogEnabled(true);
            _scCmdOutf("[Log] STOP lines enabled (every=%u)",
                (unsigned)bleGetMotorStopLogEvery());
            return;
        }
        if (strcmp(arg, "off") == 0) {
            bleSetMotorStopLogEnabled(false);
            _scCmdOut("[Log] STOP lines disabled");
            return;
        }
        if (strncmp(arg, "every ", 6) == 0) {
            int n = atoi(arg + 6);
            if (n <= 0) n = 1;
            bleSetMotorStopLogEvery((uint16_t)n);
            _scCmdOutf("[Log] STOP throttle -> every %u line(s)", (unsigned)bleGetMotorStopLogEvery());
            return;
        }
        _scCmdOut("log stop: use 'on', 'off', or 'every <N>'");
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
            _scCmdOut("assist: invalid level  0=indoor  1=outdoor  2=learning");
            return;
        }
        if (*ctx.state == STATE_OPERATING) {
            _scCmdOut("assist: ignored - motors active, stop first");
            return;
        }
        *ctx.assistLevel = (uint8_t)lvl;
        ledSetAssistLevel(*ctx.assistLevel);
        bleSendAssistLevel(*ctx.assistLevel);
        _scCmdOutf("Assist -> %s", assistConfigs[*ctx.assistLevel].name);
        return;
    }

    // hillhold <on|off>
    if (strncmp(cmd, "hillhold ", 9) == 0) {
        if (*ctx.state == STATE_OPERATING) {
            _scCmdOut("hillhold: ignored - motors active");
            return;
        }
        const char* arg = cmd + 9;
        if (strcmp(arg, "on") == 0) {
            *ctx.hillHoldOn = true;
        }
        else if (strcmp(arg, "off") == 0) {
            *ctx.hillHoldOn = false;
        }
        else {
            _scCmdOut("hillhold: use 'on' or 'off'");
            return;
        }
        ledSetHillHold(*ctx.hillHoldOn);
        bleSendHillHold(*ctx.hillHoldOn);
        _scCmdOutf("HillHold -> %s", *ctx.hillHoldOn ? "ON" : "OFF");
        return;
    }

    // recal
    if (strcmp(cmd, "recal") == 0) {
        if (*ctx.state == STATE_OPERATING) {
            _scCmdOut("recal: release joystick to center before recalibrating");
            return;
        }
        _scCmdOut("Recalibrating joystick center - keep joystick at rest...");
        ctx.fnRecalibrate();
        return;
    }

    // arm - transition PAIRED -> ARMED
    if (strcmp(cmd, "arm") == 0) {
        if (!ctx.supervisor) {
            _scCmdOut("ERROR: supervisor not available");
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState != SUPERVISOR_PAIRED) {
            _scCmdOutf("arm: must be in PAIRED state (currently %s)",
                supervisorStateToString(supState));
            return;
        }
        ctx.supervisor->requestArm();
        _scCmdOut("Armed - joystick + deadman to drive");
        return;
    }

    // disarm - transition ARMED/DRIVING -> PAIRED (safe stop first)
    if (strcmp(cmd, "disarm") == 0) {
        if (!ctx.supervisor) {
            _scCmdOut("ERROR: supervisor not available");
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState != SUPERVISOR_ARMED && supState != SUPERVISOR_DRIVING) {
            _scCmdOutf("disarm: must be ARMED or DRIVING (currently %s)",
                supervisorStateToString(supState));
            return;
        }
        ctx.supervisor->requestDisarm();
        _scCmdOut("Disarmed -> PAIRED");
        return;
    }

    // stop (software e-stop -> FAILSAFE state)
    if (strcmp(cmd, "stop") == 0) {
        if (ctx.supervisor) {
            _scCmdOut("Emergency stop");
            ctx.supervisor->requestEmergencyStop("serial stop command");
        }
        else {
            _scCmdOut("ERROR: supervisor not available");
        }
        return;
    }

    // reset - exit FAILSAFE state back to CONNECTING
    if (strcmp(cmd, "reset") == 0) {
        if (!ctx.supervisor) {
            _scCmdOut("ERROR: supervisor not available");
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState != SUPERVISOR_FAILSAFE && supState != SUPERVISOR_DISCONNECTED) {
            _scCmdOut("reset: must be in FAILSAFE or DISCONNECTED state");
            return;
        }
        _scCmdOut("Clearing failsafe, reconnecting...");
        ctx.supervisor->requestReconnect();
        return;
    }

    // reconnect - force BLE reconnect
    if (strcmp(cmd, "reconnect") == 0) {
        if (!ctx.supervisor) {
            _scCmdOut("ERROR: supervisor not available");
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState == SUPERVISOR_DRIVING) {
            _scCmdOut("reconnect: stop motors first");
            return;
        }
        if (*ctx.state == STATE_OFF) {
            _scCmdOut("reconnect: device is OFF, use 'power on' first");
            return;
        }
        _scCmdOut("Forcing reconnect...");
        ctx.supervisor->requestReconnect();
        return;
    }

    // telemetry - request fresh data from all connected wheels + print cached values
    if (strcmp(cmd, "telemetry") == 0) {
        if (!bleAnyConnected()) {
            _scCmdOut("[Telemetry] No wheels connected");
            return;
        }
        bool requestAllowed = true;
        if (ctx.supervisor) {
            SupervisorState supState = ctx.supervisor->getState();
            if (supState == SUPERVISOR_ARMED || supState == SUPERVISOR_DRIVING) {
                requestAllowed = false;
                _scCmdOutf("[Telemetry] Live request blocked in %s state (to avoid BLE write contention)",
                    supervisorStateToString(supState));
            }
        }

        if (requestAllowed) {
            // Fire async BLE requests - responses arrive via notify callbacks.
            bool sentSoc = bleRequestSOC();
            bool sentFw = bleRequestFirmwareVersion();
            bool sentOdo = bleRequestCruiseValues();
            _scCmdOutf("[Telemetry] Requests sent  SOC=%s  FW=%s  Odometer=%s",
                sentSoc ? "ok" : "fail",
                sentFw ? "ok" : "fail",
                sentOdo ? "ok" : "fail");
        }
        // Print whatever is currently in the cache
        _scCmdOut("[Telemetry] --- Cached values (from last poll) ---");
        for (int _ti = 0; _ti < WHEEL_COUNT; _ti++) {
            if (!_wheelActive(_ti)) continue;
            const char* label = (_ti == WHEEL_LEFT) ? "Left " : "Right";
            bool conn = bleIsConnected(_ti);
            char line[192];
            snprintf(line, sizeof(line), "[Telemetry]   %s: ", label);
            if (!conn) {
                strlcat(line, "not connected", sizeof(line));
                _scCmdOut(line);
                continue;
            }
            int8_t batt = bleGetBattery(_ti);
            if (batt >= 0) {
                char seg[32];
                snprintf(seg, sizeof(seg), "battery=%3d%%  ", batt);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "battery= --   ", sizeof(line));
            }
            uint8_t maj = 0, min = 0, patch = 0;
            if (bleGetFirmwareVersion(_ti, maj, min, patch)) {
                char seg[32];
                snprintf(seg, sizeof(seg), "FW=%d.%d.%d  ", maj, min, patch);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "FW=--        ", sizeof(line));
            }
            float dist = bleGetDistanceKm(_ti);
            if (dist >= 0.0f) {
                char seg[32];
                snprintf(seg, sizeof(seg), "dist=%.3f km", dist);
                strlcat(line, seg, sizeof(line));
            }
            else {
                strlcat(line, "dist=--", sizeof(line));
            }
            _scCmdOut(line);
        }
        if (requestAllowed) {
            _scCmdOut("[Telemetry] Fresh values arrive via BLE notify - run 'telemetry' again or use 'log tag telemetry on'");
        }
        else {
            _scCmdOut("[Telemetry] Cached-only output in ARMED/DRIVING. Use telemetry in PAIRED for live requests.");
        }
        return;
    }

    // disconnect - force-disconnect all wheels
    if (strcmp(cmd, "disconnect") == 0) {
        if (!ctx.supervisor) {
            _scCmdOut("ERROR: supervisor not available");
            return;
        }
        SupervisorState supState = ctx.supervisor->getState();
        if (supState == SUPERVISOR_DRIVING) {
            _scCmdOut("disconnect: stop motors first ('stop' or release joystick)");
            return;
        }
        _scCmdOut("Disconnecting all wheels...");
        ctx.supervisor->requestDisconnect();
        return;
    }

    // record - BLE traffic recorder
    if (strncmp(cmd, "record", 6) == 0) {
        const char* arg = cmd + 6;
        while (*arg == ' ') arg++;

        if (strcmp(arg, "stop") == 0) {
            if (!bleRecordIsActive()) {
                _scCmdOut("record: not currently recording");
            }
            else {
                bleRecordStop();
            }
            return;
        }

        if (strcmp(arg, "dump") == 0) {
            bleRecordDump();
            return;
        }

        if (strncmp(arg, "start", 5) == 0) {
            const char* durStr = arg + 5;
            while (*durStr == ' ') durStr++;
            uint32_t dur = (*durStr != '\0') ? (uint32_t)(atoi(durStr) * 1000) : 10000;
            if (dur == 0) dur = 10000;   // guard against 'record start 0'
            if (dur > 120000) dur = 120000;  // cap at 2 minutes
            bleRecordStart(dur);
            return;
        }

        // No sub-command: show record status
        if (bleRecordIsActive()) {
            _scCmdOutf("[Record] ACTIVE  (%d entries so far, max %d)",
                (int)bleRecordEntryCount(), BLE_RECORD_MAX);
        }
        else {
            _scCmdOutf("[Record] idle  (%d entries captured)",
                (int)bleRecordEntryCount());
            _scCmdOut("[Record] Usage:");
            _scCmdOut("  record start [N]  start recording for N seconds (default 10)");
            _scCmdOut("  record stop       stop early");
            _scCmdOut("  record dump       print captured log");
        }
        return;
    }

    // power <on|off>
    if (strncmp(cmd, "power ", 6) == 0) {
        const char* arg = cmd + 6;
        if (strcmp(arg, "off") == 0) {
            if (*ctx.state == STATE_OFF) {
                _scCmdOut("power: already off");
                return;
            }
            _scCmdOut("Turning OFF (entering deep sleep)...");
            ctx.fnEnterOff();  // Never returns - enters deep sleep
        }
        else if (strcmp(arg, "on") == 0) {
            // Deep sleep means device reboots on wake, so this command only
            // makes sense during development/testing when deep sleep is disabled
            if (*ctx.state != STATE_OFF) {
                _scCmdOut("power: already on");
                return;
            }
            _scCmdOut("Note: Device uses deep sleep. Power button causes reboot.");
            _scCmdOut("This command only works if deep sleep is disabled.");
        }
        else {
            _scCmdOut("power: use 'on' or 'off'");
        }
        return;
    }

#ifdef ENABLE_BATTERY_MONITOR
    // battery
    if (strcmp(cmd, "battery") == 0) {
        if (ctx.batteryPct) {
            _scCmdOutf("[Battery] %d %%", *ctx.batteryPct);
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
        }
        else if (strcmp(arg, "off") == 0) {
            bleSetAutoReconnect(false);
        }
        else {
            _scCmdOut("autoreconnect: use 'on' or 'off'");
        }
        return;
    }

    // setmac <left|right> <XX:XX:XX:XX:XX:XX>
    if (strncmp(cmd, "setmac ", 7) == 0) {
        const char* rest = cmd + 7;
        int idx = -1;
        if (strncmp(rest, "left ", 5) == 0) { idx = WHEEL_LEFT;  rest += 5; }
        else if (strncmp(rest, "right ", 6) == 0) { idx = WHEEL_RIGHT; rest += 6; }
        if (idx < 0) {
            _scCmdOut("setmac: setmac left <MAC>  or  setmac right <MAC>");
            return;
        }
        if (strlen(rest) != 17) {
            _scCmdOut("setmac: MAC must be XX:XX:XX:XX:XX:XX (17 chars)");
            return;
        }
        if (*ctx.state == STATE_OPERATING) {
            _scCmdOut("setmac: stop motors first");
            return;
        }
        bleSetMac(idx, rest);
        if (nvsSaveMac(idx, rest)) {
            _scCmdOut("setmac: saved to NVS (survives reboot)");
        }
        else {
            _scCmdOut("setmac: WARNING - NVS save failed; change is runtime-only");
        }
        return;
    }

    // setkey <left|right> <32 hex chars, no spaces>
    if (strncmp(cmd, "setkey ", 7) == 0) {
        const char* rest = cmd + 7;
        int idx = -1;
        if (strncmp(rest, "left ", 5) == 0) { idx = WHEEL_LEFT;  rest += 5; }
        else if (strncmp(rest, "right ", 6) == 0) { idx = WHEEL_RIGHT; rest += 6; }
        if (idx < 0) {
            _scCmdOut("setkey: setkey left <32hex>  or  setkey right <32hex>");
            return;
        }
        uint8_t newKey[16];
        if (!_scParseHex16(rest, newKey)) {
            _scCmdOut("setkey: key must be exactly 32 hex chars, no spaces/colons");
            return;
        }
        bleSetKey(idx, newKey);
        if (nvsSaveKey(idx, newKey)) {
            _scCmdOut("setkey: saved to NVS (survives reboot)");
        }
        else {
            _scCmdOut("setkey: WARNING - NVS save failed; change is runtime-only");
        }
        return;
    }

    // config show / config reset / config profile <env|default>
    if (strncmp(cmd, "config", 6) == 0) {
        const char* arg = cmd + 6;
        while (*arg == ' ') arg++;
        if (strcmp(arg, "show") == 0 || *arg == '\0') {
            nvsPrintAll();
            _scCmdOutf("[Config] Profile availability: env=%s, default=YES",
                _scProfileEnvAvailable ? "YES" : "no");
            _scCmdOutf("[Config] Build default MACs: left=%s right=%s",
                _scProfileDefaultLeftMac, _scProfileDefaultRightMac);
            if (_scProfileEnvAvailable) {
                _scCmdOutf("[Config] Env profile MACs  : left=%s right=%s",
                    _scProfileEnvLeftMac, _scProfileEnvRightMac);
            }
            _scCmdOut("[Config] Switch profile: 'config profile default' or 'config profile env'");
        }
        else if (strncmp(arg, "profile ", 8) == 0) {
            const char* profile = arg + 8;
            while (*profile == ' ') profile++;

            if (*ctx.state == STATE_OPERATING) {
                _scCmdOut("[Config] profile: stop motors first");
                return;
            }

            if (strcmp(profile, "env") == 0) {
                if (!_scProfileEnvAvailable) {
                    _scCmdOut("[Config] Profile 'env' is not available in this build.");
                    _scCmdOut("[Config] Provide M25_* values in .env and rebuild.");
                    return;
                }

                bool ok = true;
                ok &= nvsSaveMac(WHEEL_LEFT, _scProfileEnvLeftMac);
                ok &= nvsSaveMac(WHEEL_RIGHT, _scProfileEnvRightMac);
                ok &= nvsSaveKey(WHEEL_LEFT, _scProfileEnvLeftKey);
                ok &= nvsSaveKey(WHEEL_RIGHT, _scProfileEnvRightKey);

                _scApplyProfile(
                    _scProfileEnvLeftMac, _scProfileEnvRightMac,
                    _scProfileEnvLeftKey, _scProfileEnvRightKey,
                    ctx
                );

                if (ok) {
                    _scCmdOut("[Config] Profile 'env': saved to NVS and applied.");
                }
                else {
                    _scCmdOut("[Config] Profile 'env': applied, but NVS save failed (runtime-only).");
                }
                _scCmdOut("[Config] Reconnect requested.");
            }
            else if (strcmp(profile, "default") == 0) {
                nvsClearAll();
                _scApplyProfile(
                    _scProfileDefaultLeftMac, _scProfileDefaultRightMac,
                    _scProfileDefaultLeftKey, _scProfileDefaultRightKey,
                    ctx
                );
                _scCmdOut("[Config] Profile 'default': NVS cleared, build defaults applied.");
                _scCmdOut("[Config] Reconnect requested.");
            }
            else {
                _scCmdOut("config profile: use 'env' or 'default'");
            }
        }
        else if (strcmp(arg, "reset") == 0) {
            nvsClearAll();
            _scCmdOut("[Config] NVS cleared. Build defaults active on next boot.");
            _scCmdOut("[Config] Restart to apply ('restart' command).");
        }
        else {
            _scCmdOut("config: use 'config show', 'config reset', or 'config profile <env|default>'");
        }
        return;
    }

    // restart
    if (strcmp(cmd, "restart") == 0) {
        _scCmdOut("Restarting ESP32...");
        delay(200);
        ESP.restart();
        return;
    }

    _scCmdOutf("Unknown: '%s'  (type 'help')", cmd);
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
inline void serialInit(const SerialContext& ctx) {
    (void)ctx;
    _scCmdOut("[Serial] Ready - type 'help' for commands");
}

/*  Call every loop() iteration.  Handles input parsing and live output. */
inline void serialTick(const SerialContext& ctx) {

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
                    _scCmdOutf("> %s", start);   // echo
                    _scDispatch(start, ctx);
                }
                _scBufLen = 0;
            }
        }
        else {
            if (_scBufLen < (uint8_t)(sizeof(_scBuf) - 1)) {
                _scBuf[_scBufLen++] = c;
            }
            // overflow: silently drop characters beyond the buffer
        }
    }

    // --- Live joystick stream (logger tag) ---
    if (LOG_SHOULD_LOG(LogLevel::DEBUG, TAG_JOYSTICK) &&
        (millis() - _scLastJsMs >= SC_JS_INTERVAL_MS)) {
        _scLastJsMs = millis();
        _scPrintJs();
    }
}

#endif // SERIAL_COMMANDS_H
