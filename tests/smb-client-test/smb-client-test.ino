/**
 * @brief Tests the SMBClient against the test server (tests/smb-test) which
 * exports $SMB_ROOT/rw, $SMB_ROOT/ro, $SMB_ROOT/sd and the FatFs image share
 * "fat" on port 4450. The results are verified directly on the local file
 * system (fat: by reading them back with SMB).
 *
 * With SMB_SHARE (and SMB_PORT) only this share of another server (e.g.
 * Samba) is tested: its directory must be available as $SMB_ROOT/<share>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include "SMB.h"
#include "WiFi.h"

const char* host = "127.0.0.1";
uint16_t port = 4450;
std::string root;
int failures = 0;

void check(const char* name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) failures++;
}

std::string readLocal(const std::string& path) {
  std::ifstream in(root + path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string randomData(size_t len) {
  std::string data(len, 0);
  for (auto& c : data) c = (char)rand();
  return data;
}

std::string readAll(SMBFile& file, size_t chunk) {
  std::string result;
  std::vector<uint8_t> buffer(chunk);
  while (true) {
    size_t n = file.read(buffer.data(), chunk);
    if (n == 0) break;
    result.append((char*)buffer.data(), n);
  }
  return result;
}

std::string readRemote(SMBClient& smb, const char* path) {
  SMBFile f = smb.open(path);
  std::string result = f ? readAll(f, 65536) : "";
  f.close();
  return result;
}

/// local = false: the share is not a local directory, so we read back the
/// results with SMB
void testShare(SMBClient& smb, const char* share, bool canShrink,
               bool local = true) {
  std::string prefix = std::string(share) + ": ";
  std::string base = "/" + std::string(share);
  auto stored = [&](const char* path) {
    return local ? readLocal(base + path) : readRemote(smb, path);
  };
  auto name = [&](const char* text) {
    static std::string s;
    s = prefix + text;
    return s.c_str();
  };

  // small buffered writes
  SMBFile f = smb.open("/hello.txt", "w");
  check(name("open for write"), (bool)f);
  f.print("Hello ");
  f.println("SMB");
  f.close();
  check(name("buffered write"), stored("/hello.txt") == "Hello SMB\r\n");

  // byte wise read
  f = smb.open("/hello.txt");
  std::string text;
  int c;
  while ((c = f.read()) >= 0) text += (char)c;
  check(name("byte wise read"), text == "Hello SMB\r\n" && f.size() == 11);
  f.seek(6);
  check(name("seek and peek"), f.peek() == 'S' && f.available() == 5);
  f.close();

  // append
  f = smb.open("/hello.txt", "a");
  f.print("more");
  f.close();
  check(name("append"), stored("/hello.txt") == "Hello SMB\r\nmore");

  // large file
  std::string big = randomData(3000000);
  f = smb.open("/big.bin", "w");
  size_t written = f.write((const uint8_t*)big.data(), big.size());
  f.close();
  check(name("large write"),
        written == big.size() && stored("/big.bin") == big);
  f = smb.open("/big.bin");
  check(name("large read"), readAll(f, 100000) == big);
  f.seek(0);
  check(name("small chunk read"), readAll(f, 100) == big);
  f.close();

  // overwrite in the middle and truncate
  f = smb.open("/big.bin", "r+");
  f.seek(1000);
  f.write((const uint8_t*)"XYZ", 3);
  f.close();
  std::string expected = big.substr(0, 1000) + "XYZ" + big.substr(1003);
  check(name("random access write"), stored("/big.bin") == expected);
  if (canShrink) {
    // the Arduino SD API can't shrink files
    f = smb.open("/big.bin", "r+");
    check(name("truncate"), f.truncate(2000));
    f.close();
    expected.resize(2000);
    check(name("truncated content"), stored("/big.bin") == expected);
  }

  // stat and exists
  FileInfo info;
  check(name("stat"), smb.stat("/big.bin", info) &&
                          info.size == expected.size() &&
                          !info.isDirectory && info.name == "big.bin");
  check(name("exists"), smb.exists("/big.bin") && !smb.exists("/nothing"));

  // directories
  check(name("mkdir"), smb.mkdir("/dir") && smb.exists("/dir"));
  check(name("rename into directory"),
        smb.rename("/big.bin", "/dir/moved.bin") &&
            stored("/dir/moved.bin") == expected);
  int count = 0;
  bool found = false;
  smb.listDir("/dir", [&](const FileInfo& e) {
    count++;
    found = e.name == "moved.bin" && e.size == expected.size();
    return true;
  });
  check(name("listDir"), count == 1 && found);
  SMBFile dir = smb.open("/dir");
  SMBFile child = dir.openNextFile();
  check(name("openNextFile"),
        dir.isDirectory() && child && std::string(child.name()) == "moved.bin" &&
            child.size() == expected.size() && !dir.openNextFile());
  child.close();
  dir.close();
  check(name("rmdir non empty fails"), !smb.rmdir("/dir"));
  check(name("remove and rmdir"),
        smb.remove("/dir/moved.bin") && smb.rmdir("/dir") && !smb.exists("/dir"));
  check(name("remove"), smb.remove("/hello.txt") && !smb.exists("/hello.txt"));
}

/// Tests a single share of a third party server (e.g. Samba)
void testOtherServer(const char* share) {
  WiFiClient client;
  SMBClient smb(client);
  check("wrong password fails",
        !smb.begin(host, share, "user", "wrong", "", port) &&
            smb.lastStatus() == STATUS_LOGON_FAILURE);
  check("connect", smb.begin(host, share, "user", "password", "", port));
  testShare(smb, share, true);
  uint64_t total = 0, free = 0;
  check("space", smb.space(total, free) && total > 0 && free <= total);
  client.stop();
  check("reconnect", smb.exists("/") && smb.isConnected());
  smb.end();

  WiFiClient signedClient;
  SMBClient smbSigned(signedClient);
  smbSigned.setSigningRequired(true);
  check("signed connect",
        smbSigned.begin(host, share, "user", "password", "", port));
  SMBFile f = smbSigned.open("/signed.txt", "w");
  f.print("signed");
  f.close();
  check("signed write",
        readLocal("/" + std::string(share) + "/signed.txt") == "signed");
  check("signed remove", smbSigned.remove("/signed.txt"));
  smbSigned.end();
}

void setup() {
  root = getenv("SMB_ROOT") ? getenv("SMB_ROOT") : ".";
  if (getenv("SMB_PORT")) port = atoi(getenv("SMB_PORT"));
  if (getenv("SMB_DEBUG")) SMBLogger::begin(Serial, SMBLogLevel::Debug);
  srand(1);
  if (getenv("SMB_SHARE")) {
    testOtherServer(getenv("SMB_SHARE"));
    printf("%s\n", failures ? "FAILED" : "ALL TESTS PASSED");
    exit(failures ? 1 : 0);
  }
  WiFiClient client;
  SMBClient smb(client);

  check("wrong password fails",
        !smb.begin(host, "rw", "user", "wrong", "", port) &&
            smb.lastStatus() == STATUS_LOGON_FAILURE);
  check("unknown share fails",
        !smb.begin(host, "unknown", "user", "password", "", port));
  check("connect", smb.begin(host, "rw", "user", "password", "", port));
  testShare(smb, "rw", true);
  uint64_t total = 0, free = 0;
  check("space", smb.space(total, free) && total > 0 && free <= total);

  // reconnect after the connection was lost
  SMBFile open = smb.open("/reconnect.txt", "w");
  client.stop();
  check("reconnect", smb.exists("/") && smb.isConnected());
  check("files of the old connection are invalid", !open);
  smb.end();

  WiFiClient signedClient;
  SMBClient smbSigned(signedClient);
  smbSigned.setSigningRequired(true);
  check("signed connect",
        smbSigned.begin(host, "rw", "user", "password", "", port));
  SMBFile f = smbSigned.open("/signed.txt", "w");
  f.print("signed");
  f.close();
  check("signed write", readLocal("/rw/signed.txt") == "signed");
  smbSigned.end();

  // removeUser() closes the active sessions of the user
  WiFiClient tempClient;
  SMBClient temp(tempClient);
  check("temp user connect",
        temp.begin(host, "rw", "temp", "old", "", port) && temp.exists("/"));
  std::string control = root + "/control";
  FILE* cf = fopen(control.c_str(), "w");
  fputs("remove temp\n", cf);
  fclose(cf);
  for (int i = 0; i < 50 && access(control.c_str(), F_OK) == 0; i++) delay(100);
  check("removed user loses session", !temp.exists("/"));
  temp.end();

  WiFiClient roClient;
  SMBClient ro(roClient);
  check("read-only connect", ro.begin(host, "ro", "user", "password", "", port));
  f = ro.open("/file.txt");
  check("read-only read", readAll(f, 100) == "readonly\n");
  f.close();
  check("read-only write fails", !ro.open("/new.txt", "w"));
  ro.end();

  WiFiClient sdClient;
  SMBClient sd(sdClient);
  check("sd connect", sd.begin(host, "sd", "user", "password", "", port));
  testShare(sd, "sd", false);
  sd.end();

  WiFiClient fatClient;
  SMBClient fat(fatClient);
  check("fat connect", fat.begin(host, "fat", "user", "password", "", port));
  testShare(fat, "fat", true, false);
  check("fat: space", fat.space(total, free) && total > 0 && free <= total);
  check("fat: rename directory",
        fat.mkdir("/a") && fat.mkdir("/a/b") && fat.rename("/a", "/c") &&
            fat.exists("/c/b") && !fat.exists("/a"));
  check("fat: cleanup", fat.rmdir("/c/b") && fat.rmdir("/c"));
  FileInfo info;
  f = fat.open("/time.txt", "w");
  f.print("time");
  f.close();
  // FF_FS_NORTC = 1: FatFs stamps new files with FF_NORTC_YEAR
  check("fat: timestamp", fat.stat("/time.txt", info) && info.modified > 0);
  fat.remove("/time.txt");
  fat.end();

  printf("%s\n", failures ? "FAILED" : "ALL TESTS PASSED");
  exit(failures ? 1 : 0);
}

void loop() {}
