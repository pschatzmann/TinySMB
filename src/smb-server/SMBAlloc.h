#pragma once
#include <stddef.h>

#include <vector>

#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

namespace smb {

/**
 * @brief std::allocator that prefers PSRAM on the ESP32 (falls back to
 * internal RAM if PSRAM is unavailable/exhausted, and to plain `new` on
 * other platforms). Intended for buffers that can grow considerably large
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
#if defined(ESP32)
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr)
      p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_8BIT);  // fallback
    return static_cast<T*>(p);
#else
    return static_cast<T*>(::operator new(n * sizeof(T)));
#endif
  }

  void deallocate(T* p, size_t) noexcept {
    if (p == nullptr) return;
#if defined(ESP32)
    heap_caps_free(p);
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
