/**
 * @brief Desktop SMB server (Arduino Emulator): exports the directory in the
 * SMB_ROOT environment variable (default: current directory) as "share" on
 * port 4450. Test with: smbclient //localhost/share -p 4450 -U user%password
 */
#include <stdlib.h>

#include "SMB.h"
#include "WiFi.h"

WiFiServer wifiServer(4450);  // port 445 needs root permissions
SMBServer<WiFiServer> smbServer(wifiServer);
FileSystemPosix files(getenv("SMB_ROOT") ? getenv("SMB_ROOT") : ".");

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  smbServer.setServerName("DESKTOP");
  smbServer.addUser("user", "password");
  smbServer.addShare("share", files, "/", false, "Desktop share");
  smbServer.begin();
}

void loop() {
  smbServer.loop();
  delay(1);  // the emulator calls loop() continuously: avoid 100% cpu
}
