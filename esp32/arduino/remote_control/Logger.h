#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    VERBOSE = 5
};

enum LogTag : uint32_t {
    TAG_SYSTEM = (1 << 0),     // boot, reset, sleep, wake, etc
    TAG_SUPERVISOR = (1 << 1), // main loop, watchdog, task watchdog, heap, uptime, etc
    TAG_BLE = (1 << 2),        // GAP, GATT, connection events, RSSI, etc (separate concerns from RFCOMM/SPP)
    TAG_MOTOR = (1 << 3),      // speed writes, stop, drive-mode gate, failure streaks
    TAG_RFCOMM = (1 << 4),     // SPP/BT Classic transport (separate concerns from BLE)
    TAG_AUTH = (1 << 5),       // GAP pairing events (PIN, passkey, AUTH_CMPL)
    TAG_TX = (1 << 6),         // BLE TX details and stats
    TAG_CONFIG = (1 << 7),     // NVS read/write, MAC/key/profile management
    TAG_WATCHDOG = (1 << 8),   // input, link, stale-notify, arm-idle timeouts
    TAG_SAFETY = (1 << 9),     // joystick centering check, emergency stop
    TAG_BOOT = (1 << 10),      // startup banner, wake source, cold-boot vs deep-sleep
    TAG_POWER = (1 << 11),     // deep sleep entry, power on/off
    TAG_BUTTON = (1 << 12),    // button press events
    TAG_JOYSTICK = (1 << 13),  // calibration, ADC snapshots
    TAG_TELEMETRY = (1 << 14), // battery %, FW version, odometer from wheels
    TAG_RECORD = (1 << 15),    // BLE packet capture/dump
    TAG_BUZZER = (1 << 16),    // hardware init, pattern errors
    TAG_CMD = (1 << 17),       // serial REPL command feedback
    TAG_SYS = (1 << 18),       // chip info, heap, uptime, WiFi stats
    TAG_CRYPTO = (1 << 19),    // encryption/decryption errors (BLE-ENC, BLE-DEC)
    TAG_ALL = (uint32_t)-1
};

class Logger {
public:
    static Logger& instance();

    // Call once in setup() - loads level + tag mask from NVS
    void begin(LogLevel defaultLevel = LogLevel::DEBUG,
        uint32_t defaultTagMask = TAG_ALL);

    // Runtime control (also persists to NVS)
    void setLevel(LogLevel level, bool persist = true);
    void setTagMask(uint32_t tagMask, bool persist = true);

    LogLevel getLevel() const { return _level; }
    uint32_t getTagMask() const { return _tagMask; }
    bool isTagEnabled(uint32_t tag) const { return (_tagMask & tag) != 0; }
    void setTagEnabled(uint32_t tag, bool enabled, bool persist = true);

    // Core log - use the macros below instead of calling this directly
    void log(LogLevel level, uint32_t tag, const char* file, int line, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));

    static const char* levelString(LogLevel level);
    static const char* tagString(uint32_t tag);

private:
    Logger() = default;
    void loadFromNVS();
    void saveToNVS();

    LogLevel _level = LogLevel::DEBUG;
    uint32_t _tagMask = TAG_ALL;
};

// Macros
// Set to 0 to strip ALL logging at compile time (production build flag)
#ifndef LOGGING_ENABLED
#define LOGGING_ENABLED 1
#endif

#ifndef LOG_FORMAT_DETAILED
#define LOG_FORMAT_DETAILED 0
#endif

#if LOGGING_ENABLED
#define LOG_ERROR(tag, fmt, ...) Logger::instance().log(LogLevel::ERROR, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) Logger::instance().log(LogLevel::WARN, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) Logger::instance().log(LogLevel::INFO, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) Logger::instance().log(LogLevel::DEBUG, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, fmt, ...) Logger::instance().log(LogLevel::VERBOSE, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else

// Compile-time elimination - zero overhead in production
#define LOG_ERROR(tag, fmt, ...) ((void)0)
#define LOG_WARN(tag, fmt, ...) ((void)0)
#define LOG_INFO(tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...) ((void)0)
#define LOG_VERBOSE(tag, fmt, ...) ((void)0)
#endif