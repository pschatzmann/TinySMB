#pragma once
#include "SMBFileSystem.h"

namespace smb {

/**
 * @brief FileSystem adapter for the Arduino SD library API (SD.open(path,
 * mode), File::seek(), File::openNextFile() ...). It also works with SdFat
 * style classes. The open modes can be adjusted if a library uses different
 * flags.
 *
 * Example: `smb::FileSystemSD<SDClass, File> sd(SD);`
 *
 * Limitations of the Arduino SD API: no timestamps, files are renamed by
 * copying and directories can not be renamed.
 */
template <class SDType, class FileType>
class FileSystemSD : public FileSystem {
 public:
  // Note: SD.h must be included before this header to get the defaults
#if defined(FILE_READ) && defined(O_RDWR) && defined(O_CREAT) && \
    defined(O_TRUNC)
  FileSystemSD(SDType& sd, int readMode = FILE_READ, int writeMode = O_RDWR,
               int createMode = O_RDWR | O_CREAT | O_TRUNC)
#elif defined(FILE_READ) && defined(FILE_WRITE)
  FileSystemSD(SDType& sd, int readMode = FILE_READ, int writeMode = FILE_WRITE,
               int createMode = FILE_WRITE)
#else
  FileSystemSD(SDType& sd, int readMode, int writeMode, int createMode)
#endif
      : sd(sd),
        readMode(readMode),
        writeMode(writeMode),
        createMode(createMode) {
  }

  bool stat(const char* path, FileInfo& info) override {
    info.name = baseName(path);
    info.modified = 0;
    if (file && filePath == path) {
      if (fileWritable) file.flush();
      info.isDirectory = false;
      info.size = file.size();
      return true;
    }
    if (strcmp(path, "/") != 0 && !sd.exists(path)) return false;
    FileType f = sd.open(path, readMode);
    if (!f) return false;
    info.isDirectory = f.isDirectory();
    info.size = info.isDirectory ? 0 : f.size();
    f.close();
    return true;
  }

  bool listDir(const char* path,
               std::function<bool(const FileInfo&)> callback) override {
    FileType dir = sd.open(path, readMode);
    if (!dir || !dir.isDirectory()) return false;
    dir.rewindDirectory();
    FileInfo info;
    while (true) {
      FileType f = dir.openNextFile();
      if (!f) break;
      info.name = baseName(f.name());
      info.isDirectory = f.isDirectory();
      info.size = info.isDirectory ? 0 : f.size();
      f.close();
      if (!callback(info)) break;
    }
    dir.close();
    return true;
  }

  int64_t read(const char* path, uint64_t offset, uint8_t* buffer,
               size_t len) override {
    FileType* f = seekFile(path, false, offset);
    if (f == nullptr) return -1;
    int rc = f->read(buffer, len);
    return rc < 0 ? -1 : rc;
  }

  int64_t write(const char* path, uint64_t offset, const uint8_t* buffer,
                size_t len) override {
    FileType* f = openFile(path, true);
    if (f == nullptr) return -1;
    if (f->position() != offset) {
      uint64_t size = f->size();
      if (offset <= size) {
        f = seekFile(path, true, offset);
        if (f == nullptr) return -1;
      } else {
        if (!f->seek(size)) return -1;
        uint8_t zeros[512] = {0};
        for (uint64_t pos = size; pos < offset;) {
          size_t n = offset - pos > sizeof(zeros) ? sizeof(zeros)
                                                  : (size_t)(offset - pos);
          if (f->write(zeros, n) != n) return -1;
          pos += n;
        }
      }
    }
    return f->write(buffer, len);
  }

  bool createFile(const char* path) override {
    closeFile();
    if (sd.exists(path)) sd.remove(path);
    FileType f = sd.open(path, createMode);
    if (!f) return false;
    f.close();
    return true;
  }

  bool truncate(const char* path, uint64_t size) override {
    FileInfo info;
    if (!stat(path, info) || info.isDirectory) return false;
    if (size == info.size) return true;
    if (size == 0) return createFile(path);
    if (size < info.size) return false;  // not supported by the SD API
    return growWithZeros(info.size, size,
                         [&](uint64_t off, const uint8_t* b, size_t n) {
                           return write(path, off, b, n);
                         });
  }

  bool mkdir(const char* path) override { return sd.mkdir(path); }
  bool rmdir(const char* path) override { return sd.rmdir(path); }
  bool remove(const char* path) override {
    closeFile();
    return sd.remove(path);
  }

  /// The SD API does not support rename: we copy the file
  bool rename(const char* from, const char* to) override {
    closeFile();
    FileInfo info;
    if (!stat(from, info) || info.isDirectory) return false;
    if (sd.exists(to)) sd.remove(to);
    FileType src = sd.open(from, readMode);
    FileType dst = sd.open(to, createMode);
    bool ok = src && dst;
    uint8_t buffer[512];
    while (ok) {
      int n = src.read(buffer, sizeof(buffer));
      if (n <= 0) break;
      ok = dst.write(buffer, n) == (size_t)n;
    }
    src.close();
    dst.close();
    if (!ok) {
      sd.remove(to);
      return false;
    }
    return sd.remove(from);
  }

  void close(const char* path) override {
    if (filePath == path) closeFile();
  }
  void flush(const char* path) override {
    if (filePath == path && file) file.flush();
  }

 protected:
  SDType& sd;
  int readMode, writeMode, createMode;
  FileType file;
  std::string filePath;
  bool fileWritable = false;

  FileType* openFile(const char* path, bool writable) {
    if (file && filePath == path && (fileWritable || !writable)) return &file;
    closeFile();
    file = sd.open(path, writable ? writeMode : readMode);
    if (!file || file.isDirectory()) {
      closeFile();
      return nullptr;
    }
    filePath = path;
    fileWritable = writable;
    return &file;
  }

  /// Positions the cached file; some File classes can't seek after EOF, so
  /// we reopen the file if necessary
  FileType* seekFile(const char* path, bool writable, uint64_t offset) {
    FileType* f = openFile(path, writable);
    if (f == nullptr) return nullptr;
    if (f->position() == offset || f->seek(offset)) return f;
    closeFile();
    f = openFile(path, writable);
    if (f == nullptr || !f->seek(offset)) return nullptr;
    return f;
  }

  void closeFile() {
    if (file) file.close();
    filePath.clear();
  }
};

}  // namespace smb
