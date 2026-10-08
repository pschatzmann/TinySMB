/**
 * @brief Exports the SD card of an ESP32 (SD_MMC library) as network share.
 * Windows: \\<ip>\sdcard, macOS/Linux: smb://<ip>/sdcard
 */
#include <WiFi.h>

#include "SMB_SDMMC.h"

WiFiServer wifiServer(SMB_DEFAULT_PORT);
SMBServer<WiFiServer> smbServer(wifiServer);
SDMMCFileSystem sdFiles(SD_MMC);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);

  // use SD_MMC.setPins() if your board does not use the default pins
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC mount failed");
    while (true) delay(1000);
  }

  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  WiFi.setSleep(false);
  Serial.println(WiFi.localIP());

  smbServer.setServerName("ESP32");
  smbServer.addUser("user", "password");
  smbServer.addShare("sdcard", sdFiles);
  smbServer.begin();
}

void loop() { smbServer.loop(); }
