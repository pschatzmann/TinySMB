#pragma once
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(IS_DESKTOP) || defined(__linux__) || defined(__APPLE__)
#include <sys/statvfs.h>
#define SMB_HAS_STATVFS 1
#endif

#include "SMBFileSystem.h"

namespace smb {

/**
 * @brief FileSystem adapter using the POSIX API. This is used on the desktop
 * (Arduino Emulator) but can also be used on the ESP32 with the VFS mount
 * point (e.g. "/sdcard" for SD_MMC).
 *
 * Example: `smb::FileSystemPosix files("/home/pi/share");`
 */
class FileSystemPosix : public FileSystem {
 public:
  /// @param root directory on the local file system which is exported
  FileSystemPosix(const char* root = "") : root(root) {
    while (!this->root.empty() && this->root.back() == '/')
      this->root.pop_back();
  }
  ~FileSystemPosix() { closeFile(); }

  bool stat(const char* path, FileInfo& info) override {
    struct stat st;
    if (::stat(full(path).c_str(), &st) != 0) return false;
    info.name = baseName(path);
    info.isDirectory = S_ISDIR(st.st_mode);
    info.size = info.isDirectory ? 0 : (uint64_t)st.st_size;
    info.modified = (uint64_t)st.st_mtime;
    return true;
  }

  bool listDir(const char* path,
               std::function<bool(const FileInfo&)> callback) override {
    std::string dirPath = full(path);
    DIR* dir = opendir(dirPath.c_str());
    if (dir == nullptr) return false;
    FileInfo info;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      struct stat st;
      std::string entryPath = dirPath + "/" + entry->d_name;
      if (::stat(entryPath.c_str(), &st) != 0) continue;
      info.name = entry->d_name;
      info.isDirectory = S_ISDIR(st.st_mode);
      info.size = info.isDirectory ? 0 : (uint64_t)st.st_size;
      info.modified = (uint64_t)st.st_mtime;
      if (!callback(info)) break;
    }
    closedir(dir);
    return true;
  }

  int64_t read(const char* path, uint64_t offset, uint8_t* buffer,
               size_t len) override {
    int fd = openFile(path, false);
    if (fd < 0) return -1;
    return ::pread(fd, buffer, len, (off_t)offset);
  }

  int64_t write(const char* path, uint64_t offset, const uint8_t* buffer,
                size_t len) override {
    int fd = openFile(path, true);
    if (fd < 0) return -1;
    return ::pwrite(fd, buffer, len, (off_t)offset);
  }

  bool createFile(const char* path) override {
    closeFile();
    FILE* f = fopen(full(path).c_str(), "wb");
    if (f == nullptr) return false;
    fclose(f);
    return true;
  }

  bool truncate(const char* path, uint64_t size) override {
    return ::truncate(full(path).c_str(), (off_t)size) == 0;
  }

  bool mkdir(const char* path) override {
    return ::mkdir(full(path).c_str(), 0755) == 0;
  }
  bool rmdir(const char* path) override {
    return ::rmdir(full(path).c_str()) == 0;
  }
  bool remove(const char* path) override {
    if (filePath == path) closeFile();
    return ::unlink(full(path).c_str()) == 0;
  }
  bool rename(const char* from, const char* to) override {
    closeFile();
    return ::rename(full(from).c_str(), full(to).c_str()) == 0;
  }
  void close(const char* path) override {
    if (filePath == path) closeFile();
  }

  bool space(uint64_t& total, uint64_t& free) override {
#ifdef SMB_HAS_STATVFS
    struct statvfs st;
    std::string path = root.empty() ? "/" : root;
    if (::statvfs(path.c_str(), &st) != 0) return false;
    total = (uint64_t)st.f_blocks * st.f_frsize;
    free = (uint64_t)st.f_bavail * st.f_frsize;
    return true;
#else
    return false;
#endif
  }

  const char* name() override { return "NTFS"; }
  bool isCaseSensitive() override { return true; }

 protected:
  std::string root;
  std::string filePath;
  FILE* file = nullptr;  // fopen() avoids a conflict with the O_* macros of SD.h
  int fd = -1;
  bool fdWritable = false;

  std::string full(const char* path) const { return root + path; }

  int openFile(const char* path, bool writable) {
    if (fd >= 0 && filePath == path && (fdWritable || !writable)) return fd;
    closeFile();
    file = fopen(full(path).c_str(), writable ? "r+b" : "rb");
    if (file == nullptr) return -1;
    fd = fileno(file);
    struct stat st;
    if (fstat(fd, &st) != 0 || S_ISDIR(st.st_mode)) {
      closeFile();
      return -1;
    }
    filePath = path;
    fdWritable = writable;
    return fd;
  }

  void closeFile() {
    if (file != nullptr) fclose(file);
    file = nullptr;
    fd = -1;
    filePath.clear();
  }
};

}  // namespace smb
