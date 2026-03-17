#pragma once

enum class LogLevel : uint8_t {
	NONE = 0,
	ERROR = 1,
	WARN = 2,
	INFO = 3,
	DEBUG = 4,
	VERBOSE = 5
};

enum LogTag : uint32_t {
	TAG_SYSTEM = (1 << 0),
	TAG_SUPERVISOR = (1 << 1),
	TAG_BLE = (1 << 2),
	TAG_ALL = (1 << 32) - 1
};

class Logger {
  public:
	static Logger &instance();

	// Call once in setup() - loads level + tag mask from NVS
	void begin(LogLevel defaultLevel = LogLevel::DEBUG,
	           uint32_t defaultTagMask = TAG_ALL);

	// Runtime control (also persists to NVS)
	void setLevel(LogLevel level, bool persist = true);
	void setTagMask(uint32_t tagMask, bool persist = true);

	LogLevel getLevel() const { return _level; }
	uint32_t getTagMask() const { return _tagMask; }

	// Core log - use the macros below instead of calling this directly
	void log(LogLevel level, uint32_t tag, const char *file, int line, const char *fmt, ...)
	    __attribute__((format(printf, 6, 7)));

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