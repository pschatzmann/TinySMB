#pragma once
#include <stdint.h>
#include <string.h>

#include <functional>
#include <string>

namespace smb {

/// Information about a file or directory
struct FileInfo {
  std::string name;  ///< name without path
  bool isDirectory = false;
  uint64_t size = 0;
  uint64_t modified = 0;  ///< unix time in seconds, 0 if unknown
};

/**
 * @brief Storage abstraction used by the SMB server. All paths are absolute
 * (starting with '/') and use '/' as separator.
 *
 * Files are addressed by path and not by handle: SMB clients keep many
 * handles open at the same time while the embedded file systems only support
 * a few open files. Implementations are expected to cache the last used open
 * file; close() is called when the last SMB handle for a path is released.
 */
class FileSystem {
 public:
  virtual ~FileSystem() = default;

  /// Provides the information for a path; returns false if it does not exist
  virtual bool stat(const char* path, FileInfo& info) = 0;
  /// Lists the directory: the callback returns false to stop the iteration
  virtual bool listDir(const char* path,
                       std::function<bool(const FileInfo&)> callback) = 0;
  /// Reads up to len bytes at the indicated offset; returns -1 on error
  virtual int64_t read(const char* path, uint64_t offset, uint8_t* buffer,
                       size_t len) = 0;
  /// Writes len bytes at the indicated offset; returns -1 on error
  virtual int64_t write(const char* path, uint64_t offset,
                        const uint8_t* buffer, size_t len) = 0;
  /// Creates an empty file (an existing file is truncated)
  virtual bool createFile(const char* path) = 0;
  /// Changes the file size
  virtual bool truncate(const char* path, uint64_t size) = 0;
  virtual bool mkdir(const char* path) = 0;
  virtual bool rmdir(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  /// Releases any cached resources for the path
  virtual void close(const char* /*path*/) {}
  /// Flushes any buffered data for the path
  virtual void flush(const char* /*path*/) {}
  /// Provides the total and free space in bytes
  virtual bool space(uint64_t& /*total*/, uint64_t& /*free*/) {
    return false;
  }
  /// File system name reported to the clients
  virtual const char* name() { return "FAT32"; }
  /// Defines if names are case sensitive
  virtual bool isCaseSensitive() { return false; }

 protected:
  /// Returns the last path element
  static std::string baseName(const char* path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t pos = p.rfind('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
  }

  /// Fallback for truncate: grows the file by appending zeros
  template <typename WriteFn>
  static bool growWithZeros(uint64_t from, uint64_t to, WriteFn writeFn) {
    uint8_t zeros[512] = {0};
    while (from < to) {
      size_t n = (to - from) > sizeof(zeros) ? sizeof(zeros) : (to - from);
      if (writeFn(from, zeros, n) != (int64_t)n) return false;
      from += n;
    }
    return true;
  }
};

}  // namespace smb
