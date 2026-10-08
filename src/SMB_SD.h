#pragma once
/**
 * @file SMB_SD.h
 * @brief Support for the SD library: include this instead of SMB.h to export
 * a SD card with SDCardFileSystem (SDFileSystem is a macro in the ESP32 SD.h).
 */
#include <SD.h>

#include "SMB.h"

namespace smb {

#if defined(ESP32)

/// Files on a SD card driven by the ESP32 SD (SPI) library
class SDCardFileSystem : public FileSystemFS {
 public:
  explicit SDCardFileSystem(fs::SDFS& sd = SD) : FileSystemFS(sd), sd(sd) {}

  bool space(uint64_t& total, uint64_t& free) override {
    total = sd.totalBytes();
    free = total - sd.usedBytes();
    return total > 0;
  }

 protected:
  fs::SDFS& sd;
};

#else

/// Files on a SD card driven by the Arduino SD library
class SDCardFileSystem : public FileSystemSD<SDClass, File> {
 public:
  explicit SDCardFileSystem(SDClass& sd = SD) : FileSystemSD<SDClass, File>(sd) {}
};

#endif

}  // namespace smb
