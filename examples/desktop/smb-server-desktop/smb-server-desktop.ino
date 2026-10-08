/**
 * @brief Desktop SMB server (Arduino Emulator): exports the directory in the
 * SMB_ROOT environment variable (default: current directory) as "share" on
 * port 4450. Test with: smbclient //localhost/share -p 4450 -U user%password
 */
#include <stdlib.h>

#include "SMB.h"
#include "WiFi.h"

const uint16_t smbPort = 4450;  // port 445 needs root permissions
const char* smbServerName = "DESKTOP";
const char* smbUser = "user";
const char* smbPassword = "password";
const char* shareName = "share";
const char* shareRoot = "/";
const bool shareReadOnly = false;
const char* shareComment = "Desktop share";

WiFiServer wifiServer(smbPort);
SMBServer<WiFiServer> smbServer(wifiServer);
FileSystemPosix files(getenv("SMB_ROOT") ? getenv("SMB_ROOT") : ".");

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  smbServer.setServerName(smbServerName);
  smbServer.addUser(smbUser, smbPassword);
  smbServer.addShare(shareName, files, shareRoot, shareReadOnly, shareComment);
  smbServer.begin();
}

void loop() {
  smbServer.loop();
  delay(1);  // the emulator calls loop() continuously: avoid 100% cpu
}
