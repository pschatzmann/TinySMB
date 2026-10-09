#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <new>
#include <vector>

#if defined(ESP32)
#include <esp32-hal-psram.h>
#include <esp_heap_caps.h>
#if !__cpp_exceptions
#include <Esp.h>
#endif
#elif defined(ARDUINO_ARCH_RP2040) && defined(RP2350_PSRAM_CS)
// RP2350 board with PSRAM (arduino-pico core): pmalloc() allocates from PSRAM
// and free() releases both PSRAM and internal RAM blocks.
#include <Arduino.h>
#define SMB_RP2350_PSRAM 1
#endif

namespace smb {

/// Returns true if PSRAM is available for the PsramAllocator (ESP32, RP2350)
inline bool hasPsram() {
#if defined(ESP32)
  return psramFound();
#elif defined(SMB_RP2350_PSRAM)
  return rp2040.getPSRAMSize() > 0;
#else
  return false;
#endif
}

/**
 * @brief std::allocator that prefers PSRAM on the ESP32 and on RP2350 boards
 * with PSRAM (falls back to internal RAM if PSRAM is unavailable/exhausted,
 * and to plain `new` on other platforms). Intended for buffers that can grow considerably large
 * (receive buffers, cached directory listings) so that scarce internal RAM
 * is preserved for everything else.
 *
 * Note: this only moves the *container's own* backing storage to PSRAM.
 * Elements that allocate separately (e.g. the std::string inside a
 * FileInfo) still use the default allocator for their own buffer.
 */
template <class T>
struct PsramAllocator {
  using value_type = T;

  PsramAllocator() noexcept = default;
  template <class U>
  PsramAllocator(const PsramAllocator<U>&) noexcept {}

  T* allocate(size_t n) {
    if (n == 0) return nullptr;
#if defined(ESP32) || defined(SMB_RP2350_PSRAM)
    size_t bytes = n * sizeof(T);
#if defined(ESP32)
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);  // fallback
#else
    void* p = pmalloc(bytes);
    if (p == nullptr) p = malloc(bytes);  // fallback
#endif
    // std::vector (and other containers) never check the returned pointer
    // for null: a failed allocation must be reported somehow, otherwise the
    // container silently writes through a null pointer (a hard-to-diagnose
    // crash). The Arduino ESP32 and RP2040 cores build without exception
    // support (-fno-exceptions) by default, so `throw std::bad_alloc()` isn't
    // always usable here - print a clear diagnostic and abort instead of
    // crashing silently.
#if __cpp_exceptions
    if (p == nullptr) throw std::bad_alloc();
#else
    if (p == nullptr) {
#if defined(ESP32)
      unsigned freeHeap = ESP.getFreeHeap();
      unsigned freePsram = ESP.getFreePsram();
#else
      unsigned freeHeap = rp2040.getFreeHeap();
      unsigned freePsram = rp2040.getFreePSRAMHeap();
#endif
      printf("[SMB] out of memory: failed to allocate %u bytes (heap: %u, "
             "psram: %u)\n",
             (unsigned)bytes, freeHeap, freePsram);
      abort();
    }
#endif
    return static_cast<T*>(p);
#else
    return static_cast<T*>(::operator new(n * sizeof(T)));
#endif
  }

  void deallocate(T* p, size_t) noexcept {
    if (p == nullptr) return;
#if defined(ESP32)
    heap_caps_free(p);
#elif defined(SMB_RP2350_PSRAM)
    free(p);  // handles PSRAM and internal RAM blocks
#else
    ::operator delete(p);
#endif
  }
};

template <class T, class U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) {
  return true;
}
template <class T, class U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) {
  return false;
}

/// std::vector which prefers to allocate its backing storage in PSRAM
template <class T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

}  // namespace smb
