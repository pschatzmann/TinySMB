/**
 * @brief Desktop SMB server using the emulated Arduino SD library: exports the
 * current directory as "sd" on port 4450.
 * Test with: smbclient //localhost/sd -p 4450 -U user%password
 */
#include <unistd.h>

#include "WiFi.h"  // include before SD.h
#include "SMB_SD.h"

WiFiServer wifiServer(4450);
SMBServer<WiFiServer> smbServer(wifiServer);
SDCardFileSystem sdFiles(SD);
char cwd[512];

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  SD.begin();
  smbServer.addUser("user", "password");
  // the emulated SD library works on the local file system
  smbServer.addShare("sd", sdFiles, getcwd(cwd, sizeof(cwd)));
  smbServer.begin();
}

void loop() {
  smbServer.loop();
  delay(1);
}
