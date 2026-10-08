#pragma once
#ifdef ESP32
#include <FS.h>
#include <unistd.h>

#include "SMBFileSystem.h"

namespace smb {

/**
 * @brief FileSystem adapter for the ESP32 fs::FS API. This covers SD (SPI),
 * SD_MMC, LittleFS, SPIFFS and FFat.
 *
 * Example: `smb::FileSystemFS sdmmc(SD_MMC);`
 */
class FileSystemFS : public FileSystem {
 public:
  FileSystemFS(fs::FS& fs) : fs(fs) {}

  /// Optional: report total and used bytes (e.g. from SD_MMC.totalBytes())
  void setSpaceCallback(std::function<bool(uint64_t&, uint64_t&)> cb) {
    spaceCallback = cb;
  }

  bool stat(const char* path, FileInfo& info) override {
    info.name = baseName(path);
    if (file && filePath == path) {
      // avoid opening the same file twice
      if (fileWritable) file.flush();
      info.isDirectory = false;
      info.size = file.size();
      info.modified = toUnix(file.getLastWrite());
      return true;
    }
    if (strcmp(path, "/") != 0 && !fs.exists(path)) return false;
    fs::File f = fs.open(path, FILE_READ);
    if (!f) return false;
    info.isDirectory = f.isDirectory();
    info.size = info.isDirectory ? 0 : f.size();
    info.modified = toUnix(f.getLastWrite());
    f.close();
    return true;
  }

  bool listDir(const char* path,
               std::function<bool(const FileInfo&)> callback) override {
    fs::File dir = fs.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) return false;
    FileInfo info;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      info.name = baseName(f.name());
      info.isDirectory = f.isDirectory();
      info.size = info.isDirectory ? 0 : f.size();
      info.modified = toUnix(f.getLastWrite());
      f.close();
      if (!callback(info)) break;
    }
    dir.close();
    return true;
  }

  int64_t read(const char* path, uint64_t offset, uint8_t* buffer,
               size_t len) override {
    fs::File* f = openFile(path, false);
    if (f == nullptr) return -1;
    if (f->position() != offset && !f->seek(offset)) return -1;
    return f->read(buffer, len);
  }

  int64_t write(const char* path, uint64_t offset, const uint8_t* buffer,
                size_t len) override {
    fs::File* f = openFile(path, true);
    if (f == nullptr) return -1;
    if (f->position() != offset) {
      size_t size = f->size();
      if (offset > size) {
        // the file must grow: fill the gap with zeros
        if (!f->seek(size)) return -1;
        uint8_t zeros[512] = {0};
        for (uint64_t pos = size; pos < offset;) {
          size_t n = offset - pos > sizeof(zeros) ? sizeof(zeros)
                                                  : (size_t)(offset - pos);
          if (f->write(zeros, n) != n) return -1;
          pos += n;
        }
      } else if (!f->seek(offset)) {
        return -1;
      }
    }
    return f->write(buffer, len);
  }

  bool createFile(const char* path) override {
    closeFile();
    fs::File f = fs.open(path, FILE_WRITE, true);
    if (!f) return false;
    f.close();
    return true;
  }

  bool truncate(const char* path, uint64_t size) override {
    closeFile();
    const char* mp = fs.mountpoint();
    if (mp != nullptr) {
      // VFS based file systems support the posix API
      std::string full = std::string(mp) + path;
      return ::truncate(full.c_str(), size) == 0;
    }
    FileInfo info;
    if (!stat(path, info)) return false;
    if (size == 0) return createFile(path);
    if (size == info.size) return true;
    if (size < info.size) return false;
    return growWithZeros(info.size, size,
                         [&](uint64_t off, const uint8_t* b, size_t n) {
                           return write(path, off, b, n);
                         });
  }

  bool mkdir(const char* path) override { return fs.mkdir(path); }
  bool rmdir(const char* path) override { return fs.rmdir(path); }
  bool remove(const char* path) override {
    closeFile();
    return fs.remove(path);
  }
  bool rename(const char* from, const char* to) override {
    closeFile();
    return fs.rename(from, to);
  }

  void close(const char* path) override {
    if (filePath == path) closeFile();
  }
  void flush(const char* path) override {
    if (filePath == path && file) file.flush();
  }

  bool space(uint64_t& total, uint64_t& free) override {
    if (spaceCallback) return spaceCallback(total, free);
    return false;
  }

 protected:
  fs::FS& fs;
  fs::File file;
  std::string filePath;
  bool fileWritable = false;
  std::function<bool(uint64_t&, uint64_t&)> spaceCallback;

  static uint64_t toUnix(time_t t) { return t > 0 ? (uint64_t)t : 0; }

  fs::File* openFile(const char* path, bool writable) {
    if (file && filePath == path && (fileWritable || !writable)) return &file;
    closeFile();
    file = fs.open(path, writable ? "r+" : FILE_READ);
    if (!file || file.isDirectory()) {
      closeFile();
      return nullptr;
    }
    filePath = path;
    fileWritable = writable;
    return &file;
  }

  void closeFile() {
    if (file) file.close();
    filePath.clear();
  }
};

}  // namespace smb
#endif
