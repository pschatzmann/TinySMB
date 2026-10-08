/**
 * @brief Accesses a Windows/Samba share from an ESP32 with the fs::FS API:
 * lists the root directory, writes a file and reads it back.
 */
#include <WiFi.h>

#include "SMB.h"

const char* ssid = "ssid";
const char* wifiPassword = "password";
const char* smbHost = "192.168.1.10";
const char* smbShare = "share";
const char* smbUser = "user";
const char* smbPassword = "password";

WiFiClient wifiClient;
SMBClient smbClient(wifiClient);
SMBFS smbFS(smbClient);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  WiFi.begin(ssid, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  WiFi.setSleep(false);

  // \\192.168.1.10\share
  if (!smbClient.begin(smbHost, smbShare, smbUser, smbPassword)) {
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
