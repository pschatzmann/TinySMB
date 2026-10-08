#pragma once
#include <Arduino.h>
#include <time.h>

#if defined(IS_DESKTOP)
#include <random>
#endif
#if defined(ESP32)
#include <esp_random.h>
#endif

namespace smb {

/**
 * @brief Platform specific helpers: random numbers and wall clock time
 */
class Platform {
 public:
  static uint32_t random32() {
#if defined(ESP32)
    return esp_random();
#elif defined(IS_DESKTOP)
    static std::random_device rd;
    return rd();
#else
    return ((uint32_t)::random(0x10000) << 16) ^ (uint32_t)::random(0x10000) ^
           micros();
#endif
  }

  static uint8_t randomByte() { return (uint8_t)random32(); }

  /// Current unix time in seconds (0 if unknown)
  static uint64_t unixTime() {
#if defined(ESP32) || defined(IS_DESKTOP) || defined(ARDUINO_ARCH_RP2040)
    time_t t = time(nullptr);
    // values before 2000 indicate that the clock was never set
    return t > 946684800 ? (uint64_t)t : 0;
#else
    return 0;
#endif
  }
};

}  // namespace smb
