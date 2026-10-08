/**
 * @brief Test server for tests/smbclient-test.sh: exports $SMB_ROOT/rw
 * (posix, read/write), $SMB_ROOT/ro (read-only) and $SMB_ROOT/sd (emulated SD
 * library) and $SMB_ROOT/fat.img (arduino-fatfs disk image) as share "fat" on
 * port 4450. Guest access is disabled. The user "reader" only has read
 * access.
 *
 * The users can be changed at runtime by writing a line to
 * $SMB_ROOT/control: "passwd <user> <password>" or "remove <user>".
 */
#include <stdlib.h>

#include "WiFi.h"  // include before SD.h
#include "SMB_SD.h"
// only the FatFs API: fatfs.h would conflict with the emulated SD library
#include "driver/FileIO.h"
#include "smb-server/SMBFileSystemFatFs.h"

WiFiServer wifiServer(4450);
SMBServer<WiFiServer> smbServer(wifiServer);
FileSystemPosix files;
SDCardFileSystem sdFiles(SD);
std::string root;
std::string imagePath;
fatfs::FileIO* fatDisk = nullptr;
fatfs::FatFs fat;
FileSystemFatFs fatFiles(fat);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  root = getenv("SMB_ROOT") ? getenv("SMB_ROOT") : ".";
  smbServer.addUser("user", "password");
  smbServer.addReadOnlyUser("reader", "secret");
  smbServer.addUser("temp", "old");
  smbServer.setTimingLog(true);
  smbServer.addShare("rw", files, (root + "/rw").c_str());
  smbServer.addShare("ro", files, (root + "/ro").c_str(), true);
  smbServer.addShare("sd", sdFiles, (root + "/sd").c_str());
  // 64 MB image: formatted when it does not exist
  imagePath = root + "/fat.img";
  fatDisk = new fatfs::FileIO(imagePath.c_str(), 131072);
  fat.setDriver(*fatDisk);
  if (fatDisk->mount(fat) == fatfs::FR_OK) {
    smbServer.addShare("fat", fatFiles);
  } else {
    Serial.println("could not mount fat.img");
  }
  smbServer.begin();
}

/// Executes the command in $SMB_ROOT/control and deletes the file
void processControlFile() {
  std::string path = root + "/control";
  FILE* f = fopen(path.c_str(), "r");
  if (f == nullptr) return;
  char cmd[32] = {0}, user[64] = {0}, password[64] = {0};
  int n = fscanf(f, "%31s %63s %63s", cmd, user, password);
  fclose(f);
  ::remove(path.c_str());
  bool ok = false;
  if (n == 3 && strcmp(cmd, "passwd") == 0) {
    ok = smbServer.setPassword(user, password);
  } else if (n >= 2 && strcmp(cmd, "remove") == 0) {
    ok = smbServer.removeUser(user);
  }
  Serial.print("control: ");
  Serial.print(cmd);
  Serial.println(ok ? " ok" : " failed");
}

void loop() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 50) {
    lastCheck = millis();
    processControlFile();
  }
  smbServer.loop();
  delay(1);
}
