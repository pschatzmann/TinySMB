#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace smb {

enum class SMBLogLevel { None = 0, Error, Info, Debug };

/**
 * @brief Minimal logger: call SMBLogger.begin(Serial, SMBLogLevel::Info) to
 * activate the output. `SMBLogger` is a ready-to-use static instance of
 * `SMBLoggerClass`.
 */
class SMBLoggerClass {
 public:
  void begin(Print& out, SMBLogLevel level = SMBLogLevel::Info) {
    output() = &out;
    logLevel() = level;
  }

  bool isActive(SMBLogLevel level) {
    return output() != nullptr && level != SMBLogLevel::None &&
           level <= logLevel();
  }

  void log(SMBLogLevel level, const char* fmt, ...) {
    if (!isActive(level)) return;
    char msg[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    Print& out = *output();
    out.print("[SMB] ");
    out.print(levelName(level));
    out.print(": ");
    out.println(msg);
  }

 protected:
  Print*& output() {
    static Print* out = nullptr;
    return out;
  }
  SMBLogLevel& logLevel() {
    static SMBLogLevel level = SMBLogLevel::Info;
    return level;
  }
  const char* levelName(SMBLogLevel level) {
    switch (level) {
      case SMBLogLevel::Error:
        return "E";
      case SMBLogLevel::Info:
        return "I";
      default:
        return "D";
    }
  }
};

/// Static, ready-to-use logger instance: SMBLogger.begin(Serial, ...);
static SMBLoggerClass SMBLogger;

}  // namespace smb

#define SMB_LOGE(...) smb::SMBLogger.log(smb::SMBLogLevel::Error, __VA_ARGS__)
#define SMB_LOGI(...) smb::SMBLogger.log(smb::SMBLogLevel::Info, __VA_ARGS__)
#define SMB_LOGD(...) smb::SMBLogger.log(smb::SMBLogLevel::Debug, __VA_ARGS__)
