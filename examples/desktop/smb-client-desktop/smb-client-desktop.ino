/**
 * @brief Desktop SMB client (Arduino Emulator): lists the share
 * \\127.0.0.1\share on port 4450 (e.g. provided by smb-server-desktop) and
 * writes a file.
 */
#include <stdlib.h>

#include "SMB.h"
#include "WiFi.h"

WiFiClient client;
SMBClient smbClient(client);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  if (!smbClient.begin("127.0.0.1", "share", "user", "password", "", 4450)) exit(1);

  smbClient.listDir("/", [](const FileInfo& info) {
    Serial.print(info.isDirectory ? "<DIR> " : "      ");
    Serial.println(info.name.c_str());
    return true;
  });

  SMBFile file = smbClient.open("/from-client.txt", "w");
  file.println("Hello from the SMB client");
  file.close();
  smbClient.end();
  exit(0);
}

void loop() {}
