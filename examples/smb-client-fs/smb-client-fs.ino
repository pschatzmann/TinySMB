/**
 * @brief Accesses a Windows/Samba share from an ESP32 with the fs::FS API:
 * lists the root directory, writes a file and reads it back.
 */
#include <WiFi.h>

#include "SMB.h"

WiFiClient wifiClient;
SMBClient smbClient(wifiClient);
SMBFS smbFS(smbClient);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  WiFi.setSleep(false);

  // \\192.168.1.10\share
  if (!smbClient.begin("192.168.1.10", "share", "user", "password")) {
    Serial.println("connect failed");
    return;
  }

  File root = smbFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    Serial.printf("%s %u\n", f.name(), (unsigned)f.size());
  }

  File out = smbFS.open("/hello.txt", FILE_WRITE);
  out.println("Hello from the ESP32");
  out.close();

  File in = smbFS.open("/hello.txt");
  while (in.available()) Serial.write(in.read());
  in.close();
}

void loop() {}
