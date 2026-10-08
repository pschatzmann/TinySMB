#pragma once
/**
 * @file SMB_SDMMC.h
 * @brief Support for the ESP32 SD_MMC library (SDMMC host): include this
 * instead of SMB.h to export a SD card with SDMMCFileSystem.
 */
#include <SD_MMC.h>
#include "sdkconfig.h"

#include "SMB.h"

namespace smb {

/// Files on a SD card driven by the SD_MMC library
class SDMMCFileSystem : public FileSystemFS {
 public:
  // Note: this object is usually instantiated as a global, i.e. before
  // setup() runs Serial.begin()/SMBLogger.begin(), so SMB_LOGx calls made
  // here are silently dropped (the logger is not active yet). Call
  // logPinSetup() from setup() (after SMBLogger.begin()) if you want to
  // confirm the pin assignment and setPins() result in the log.
  explicit SDMMCFileSystem(fs::SDMMCFS& sd = SD_MMC) : FileSystemFS(sd), sd(sd) {}

  /// Remaps the SD_MMC pins (4-bit mode) before SD_MMC.begin() is called.
  /// Needed on boards where the default pins are not usable, e.g. because
  /// they are shared with a camera or with PSRAM/OPI flash.
  SDMMCFileSystem(fs::SDMMCFS& sd, int clk, int cmd, int d0, int d1, int d2,
                 int d3)
      : FileSystemFS(sd), sd(sd), hasPins(true), clk(clk), cmd(cmd), d0(d0),
        d1(d1), d2(d2), d3(d3) {
    pinsOk = sd.setPins(clk, cmd, d0, d1, d2, d3);
  }

  bool space(uint64_t& total, uint64_t& free) override {
    total = sd.totalBytes();
    free = total - sd.usedBytes();
    return total > 0;
  }

  /// Logs the pin assignment and the result of setPins(); call this from
  /// setup(), after SMBLogger.begin(), to see the result in the log.
  void logPinSetup() {
    if (!hasPins) {
      SMB_LOGI("SDMMCFileSystem: using default SD_MMC pins");
      return;
    }
    SMB_LOGI("setting pins: clk=%d, cmd=%d, d0=%d, d1=%d, d2=%d, d3=%d", clk,
             cmd, d0, d1, d2, d3);
    if (!pinsOk) SMB_LOGE("Pin change failed!");
  }

 protected:
  fs::SDMMCFS& sd;
  bool hasPins = false;
  bool pinsOk = true;
  int clk = 0, cmd = 0, d0 = 0, d1 = 0, d2 = 0, d3 = 0;
};


}  // namespace smb
