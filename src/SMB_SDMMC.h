#pragma once
/**
 * @file SMB_SDMMC.h
 * @brief Support for the ESP32 SD_MMC library (SDMMC host): include this
 * instead of SMB.h to export a SD card with SDMMCFileSystem.
 */
#include <SD_MMC.h>

#include "SMB.h"

namespace smb {

/// Files on a SD card driven by the SD_MMC library
class SDMMCFileSystem : public FileSystemFS {
 public:
  explicit SDMMCFileSystem(fs::SDMMCFS& sd = SD_MMC) : FileSystemFS(sd), sd(sd) {}

  bool space(uint64_t& total, uint64_t& free) override {
    total = sd.totalBytes();
    free = total - sd.usedBytes();
    return total > 0;
  }

 protected:
  fs::SDMMCFS& sd;
};

}  // namespace smb
