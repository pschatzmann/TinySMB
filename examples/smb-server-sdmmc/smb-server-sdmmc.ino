/**
 * @brief Exports the SD card of an ESP32 (SD_MMC library) as network share.
 * Windows: \\<ip>\sdcard, macOS/Linux: smb://<ip>/sdcard
 */
#include <WiFi.h>

#include "SMB_SDMMC.h"

const char* ssid = "ssid";
const char* wifiPassword = "password";
const char* smbUser = "user";
const char* smbPassword = "password";
const char* smbServerName = "ESP32";
const char* shareName = "sdcard";
const char* sdMountPoint = "/sdcard";
const bool sdFormatIfFailed = true;

WiFiServer wifiServer(SMB_DEFAULT_PORT);
SMBServer<WiFiServer> smbServer(wifiServer);
SDMMCFileSystem sdFiles(SD_MMC);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);

  // use SD_MMC.setPins() if your board does not use the default pins
  if (!SD_MMC.begin(sdMountPoint, sdFormatIfFailed)) {
    Serial.println("SD_MMC mount failed");
    while (true) delay(1000);
  }

  WiFi.begin(ssid, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) delay(500);
#ifdef ESP32
  WiFi.setSleep(false);
#endif
  Serial.println(WiFi.localIP());

  smbServer.setServerName(smbServerName);
  smbServer.addUser(smbUser, smbPassword);
  smbServer.addShare(shareName, sdFiles);
  smbServer.begin();
}

void loop() { smbServer.loop(); }
