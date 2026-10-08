#pragma once
#ifdef ESP32
#include <FS.h>
#include <FSImpl.h>

#include "SMBClient.h"

namespace smb {

/// fs::FileImpl on top of a SMBFile
class SMBFileImpl : public fs::FileImpl {
 public:
  SMBFileImpl(const SMBFile& file) : file(file), filePath(file.path()) {}

  size_t write(const uint8_t* buf, size_t size) override {
    return file.write(buf, size);
  }
  size_t read(uint8_t* buf, size_t size) override {
    return file.read(buf, size);
  }
  void flush() override { file.flush(); }
  bool seek(uint32_t pos, fs::SeekMode mode) override {
    uint64_t target = pos;
    if (mode == fs::SeekCur) target = file.position() + pos;
    if (mode == fs::SeekEnd) target = file.size() + pos;
    return file.seek(target);
  }
  size_t position() const override { return file.position(); }
  size_t size() const override { return file.size(); }
  bool setBufferSize(size_t size) override {
    file.setBufferSize(size);
    return true;
  }
  void close() override { file.close(); }
  time_t getLastWrite() override { return file.getLastWrite(); }
  const char* path() const override { return filePath.c_str(); }
  const char* name() const override { return file.name(); }
  boolean isDirectory(void) override { return file.isDirectory(); }
  fs::FileImplPtr openNextFile(const char* mode) override {
    SMBFile next = file.openNextFile(mode);
    if (!next) return fs::FileImplPtr();
    return std::make_shared<SMBFileImpl>(next);
  }
  boolean seekDir(long position) override {
    file.rewindDirectory();
    FileInfo info;
    for (long i = 0; i < position; i++)
      if (!file.getNextEntry(info)) return false;
    return true;
  }
  String getNextFileName(void) override {
    bool isDir;
    return getNextFileName(&isDir);
  }
  String getNextFileName(bool* isDir) override {
    FileInfo info;
    if (!file.getNextEntry(info)) return "";
    if (isDir) *isDir = info.isDirectory;
    std::string p = filePath == "/" ? "/" + info.name
                                    : filePath + "/" + info.name;
    return String(p.c_str());
  }
  void rewindDirectory(void) override { file.rewindDirectory(); }
  operator bool() override { return (bool)file; }

 protected:
  SMBFile file;
  std::string filePath;
};

/// fs::FSImpl on top of a SMBClient
class SMBFSImpl : public fs::FSImpl {
 public:
  SMBFSImpl(SMBClient& client) : client(client) {}

  fs::FileImplPtr open(const char* path, const char* mode,
                       const bool create) override {
    if (create && mode && mode[0] != 'r') makeParents(path);
    SMBFile file = client.open(path, mode);
    if (!file) return fs::FileImplPtr();
    return std::make_shared<SMBFileImpl>(file);
  }
  bool exists(const char* path) override { return client.exists(path); }
  bool rename(const char* from, const char* to) override {
    return client.rename(from, to);
  }
  bool remove(const char* path) override { return client.remove(path); }
  bool mkdir(const char* path) override { return client.mkdir(path); }
  bool rmdir(const char* path) override { return client.rmdir(path); }

 protected:
  SMBClient& client;

  void makeParents(const char* path) {
    std::string p = path;
    for (size_t pos = p.find('/', 1); pos != std::string::npos;
         pos = p.find('/', pos + 1)) {
      std::string dir = p.substr(0, pos);
      if (!client.exists(dir.c_str())) client.mkdir(dir.c_str());
    }
  }
};

/**
 * @brief SMB share as ESP32 file system: it can be used like SD or LittleFS.
 *
 * Example: `SMBFS smbFS(smbClient); File f = smbFS.open("/test.txt");`
 */
class SMBFS : public fs::FS {
 public:
  explicit SMBFS(SMBClient& client)
      : fs::FS(std::make_shared<SMBFSImpl>(client)) {}
};

}  // namespace smb
#endif
