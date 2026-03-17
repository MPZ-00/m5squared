#include "Logger.h"
#include <Preferences.h>
#include <stdarg.h>

static const char* NVS_NS = "logger";
static const char* NVS_LEVEL = "level";
static const char* NVS_TAG_MASK = "tagmask";

const char* Logger::levelString(LogLevel l) {
    switch (l) {
    case LogLevel::ERROR:
        return "E";
    case LogLevel::WARN:
        return "W";
    case LogLevel::INFO:
        return "I";
    case LogLevel::DEBUG:
        return "D";
    case LogLevel::VERBOSE:
        return "V";
    default:
        return "?";
    }
}

const char* Logger::tagString(uint32_t tag) {
    switch (tag) {
    case TAG_SYSTEM: return "SYSTEM";
    case TAG_SUPERVISOR: return "SUPERVISOR";
    case TAG_BLE: return "BLE";
    case TAG_MOTOR: return "MOTOR";
    case TAG_RFCOMM: return "RFCOMM";
    case TAG_AUTH: return "AUTH";
    case TAG_TX: return "TX";
    case TAG_CONFIG: return "CONFIG";
    case TAG_WATCHDOG: return "WATCHDOG";
    case TAG_SAFETY: return "SAFETY";
    case TAG_BOOT: return "BOOT";
    case TAG_POWER: return "POWER";
    case TAG_BUTTON: return "BUTTON";
    case TAG_JOYSTICK: return "JOYSTICK";
    case TAG_TELEMETRY: return "TELEMETRY";
    case TAG_RECORD: return "RECORD";
    case TAG_BUZZER: return "BUZZER";
    case TAG_CMD: return "CMD";
    case TAG_SYS: return "SYS";
    case TAG_CRYPTO: return "CRYPTO";
    default: return "GEN";
    }
}

static const char* filename(const char* path) {
    if (!path) return "?";
    const char* slash = strrchr(path, '/');
    const char* bslash = strrchr(path, '\\');
    const char* s = slash;
    if (!s || (bslash && bslash > s)) {
        s = bslash;
    }
    return s ? s + 1 : path;
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::begin(LogLevel defaultLevel, uint32_t defaultTagMask) {
    _level = defaultLevel;
    _tagMask = defaultTagMask;
    loadFromNVS();	// Overrides defaults if persisted values exist
}

void Logger::setLevel(LogLevel level, bool persist) {
    _level = level;
    if (persist) { saveToNVS(); }
}

void Logger::setTagMask(uint32_t mask, bool persist) {
    _tagMask = mask;
    if (persist) { saveToNVS(); }
}

void Logger::setTagEnabled(uint32_t tag, bool enabled, bool persist) {
    if (enabled) {
        _tagMask |= tag;
    }
    else {
        _tagMask &= ~tag;
    }
    if (persist) { saveToNVS(); }
}

void Logger::log(LogLevel level, uint32_t tag, const char* file, int line, const char* fmt, ...) {
    // Level gate
    if (level > _level) return;
    if ((_tagMask & tag) == 0) return;

    // Format user message
    char msgBuf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    // Output: compact by default, optionally include source location.
#if LOG_FORMAT_DETAILED
    Serial.printf("[%s][%s][%s:%d] %s\n", levelString(level), tagString(tag), filename(file), line, msgBuf);
#else
    Serial.printf("[%s][%s] %s\n", levelString(level), tagString(tag), msgBuf);
#endif
}

void Logger::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/true)) return;

    _level = static_cast<LogLevel>(
        prefs.getUChar(NVS_LEVEL, static_cast<uint8_t>(_level)));
    _tagMask = prefs.getUInt(NVS_TAG_MASK, _tagMask);

    prefs.end();
}

void Logger::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return;

    prefs.putUChar(NVS_LEVEL, static_cast<uint8_t>(_level));
    prefs.putUInt(NVS_TAG_MASK, _tagMask);

    prefs.end();
}