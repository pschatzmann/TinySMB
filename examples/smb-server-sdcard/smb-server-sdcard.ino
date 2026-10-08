/**
 * @brief Exports a SD card (SD library) as network share.
 * Windows: \\<ip>\sd, macOS/Linux: smb://<ip>/sd
 */
#include <WiFi.h>

#include "SMB_SD.h"

const char* ssid = "ssid";
const char* wifiPassword = "password";
const char* smbUser = "user";
const char* smbPassword = "password";
const char* shareName = "sd";
const uint8_t sdChipSelectPin = SS;

WiFiServer wifiServer(SMB_DEFAULT_PORT);
SMBServer<WiFiServer> smbServer(wifiServer);
SDCardFileSystem sdFiles(SD);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);

  if (!SD.begin(sdChipSelectPin)) {
    Serial.println("SD mount failed");
    while (true) delay(1000);
  }

  WiFi.begin(ssid, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) delay(500);
#ifdef ESP32
  WiFi.setSleep(false);
#endif
  Serial.println(WiFi.localIP());

  smbServer.addUser(smbUser, smbPassword);
  smbServer.addShare(shareName, sdFiles);
  smbServer.begin();
}

void loop() { smbServer.loop(); }
