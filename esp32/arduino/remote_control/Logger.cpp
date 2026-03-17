#include "Logger.h"
#include <Preferences.h>
#include <stdarg.h>

static const char* NVS_NS = "logger";
static const char* NVS_LEVEL = "level";
static const char* NVS_TAG_MASK = "tagmask";

static const char* levelStr(LogLevel l) {
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

static const char* filename(const char* path) {
	const char* s = strchr(path, '/');
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

	// Output: [L][file:line] message
	Serial.printf("[%s][%s:%d] %s\n", levelStr(level), filename(file), line, msgBuf);
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