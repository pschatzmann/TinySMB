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
const bool mode1Bit = false;
const bool sdFormatIfFailed = true;

WiFiServer wifiServer(SMB_DEFAULT_PORT);
SMBServer<WiFiServer> smbServer(wifiServer);
// Use the default constructor SDMMCFileSystem sdFiles(SD_MMC); if your board
// uses the default SD_MMC pins. If it doesn't (e.g. the pins are shared with
// a camera or with PSRAM/OPI flash), pass the pins used by your board:
SDMMCFileSystem sdFiles(SD_MMC, /*clk*/42, /*cmd*/39, /*d0*/41, /*d1*/40,
                       /*d2*/37, /*d3*/38);

void setup() {
  Serial.begin(115200);
  // On boards with native USB (e.g. ESP32-S3 USB-OTG/CDC), wait for the
  // host to attach so early log output is not lost. Make sure your
  // terminal is connected to the native-USB port, not the UART0 bridge
  // port (boot/ROM messages always go out UART0, independent of this).
  while (!Serial) delay(10);
  SMBLogger.begin(Serial, SMBLogLevel::Info);
  // logs the pin assignment and whether setPins() succeeded
  sdFiles.logPinSetup();

  // use SD_MMC.setPins() if your board does not use the default pins
  if (!SD_MMC.begin(sdMountPoint, mode1Bit, sdFormatIfFailed)) {
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
