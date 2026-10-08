#pragma once
// arduino-fatfs: only the FatFs API, not the SD wrapper (IO.h includes ff.h)
#include "driver/IO.h"

#include "SMBFileSystem.h"

namespace smb {

/**
 * @brief FileSystem adapter for the FatFs API of the arduino-fatfs library
 * (https://github.com/pschatzmann/arduino-fatfs). It works with any of its
 * drivers (SPI or SDMMC SD cards, RAM disks, disk images...) and supports
 * timestamps, rename of files and directories, truncate and the free space.
 *
 * Example: `smb::FileSystemFatFs fat(*SD.getFatFs());`
 *
 * Timestamps of new and changed files are only set if FF_FS_NORTC is 0 and
 * get_fattime() is provided. Names are converted with FF_CODE_PAGE: set
 * FF_LFN_UNICODE to 2 (UTF-8) to support all characters.
 */
class FileSystemFatFs : public FileSystem {
 public:
  /// @param drive logical drive prefix (e.g. "1:") if the volume is not 0
  FileSystemFatFs(fatfs::FatFs& fs, const char* drive = "")
      : fs(fs), drive(drive) {}
  ~FileSystemFatFs() { closeFile(); }

  bool stat(const char* path, FileInfo& info) override {
    info.name = baseName(path);
    if (strcmp(path, "/") == 0) {
      // FatFs can't stat the root directory
      info.isDirectory = true;
      info.size = 0;
      info.modified = 0;
      return true;
    }
    fatfs::FILINFO fi;
    if (fs.f_stat(full(path).c_str(), &fi) != fatfs::FR_OK) return false;
    toInfo(fi, info);
    // the directory entry is only updated by f_sync()
    if (isOpen && filePath == path) info.size = fs.f_size(&file);
    return true;
  }

  bool listDir(const char* path,
               std::function<bool(const FileInfo&)> callback) override {
    fatfs::DIR dir;
    if (fs.f_opendir(&dir, full(path).c_str()) != fatfs::FR_OK) return false;
    fatfs::FILINFO fi;
    FileInfo info;
    while (fs.f_readdir(&dir, &fi) == fatfs::FR_OK && fi.fname[0] != 0) {
      if (strcmp(fi.fname, ".") == 0 || strcmp(fi.fname, "..") == 0) continue;
      info.name = fi.fname;
      toInfo(fi, info);
      if (!callback(info)) break;
    }
    fs.f_closedir(&dir);
    return true;
  }

  int64_t read(const char* path, uint64_t offset, uint8_t* buffer,
               size_t len) override {
    fatfs::FIL* f = openFile(path, false);
    if (f == nullptr || !seek(offset)) return -1;
    UINT n = 0;
    if (fs.f_read(f, buffer, (UINT)len, &n) != fatfs::FR_OK) return -1;
    return n;
  }

  int64_t write(const char* path, uint64_t offset, const uint8_t* buffer,
                size_t len) override {
    fatfs::FIL* f = openFile(path, true);
    if (f == nullptr) return -1;
    uint64_t size = fs.f_size(f);
    if (offset > size) {
      // FatFs leaves the gap undefined: fill it with zeros
      if (!seek(size)) return -1;
      uint8_t zeros[512] = {0};
      for (uint64_t pos = size; pos < offset;) {
        size_t n = offset - pos > sizeof(zeros) ? sizeof(zeros)
                                                : (size_t)(offset - pos);
        if (writeAll(zeros, n) != (int64_t)n) return -1;
        pos += n;
      }
    } else if (!seek(offset)) {
      return -1;
    }
    return writeAll(buffer, len);
  }

  bool createFile(const char* path) override {
    closeFile();
    fatfs::FIL f;
    if (fs.f_open(&f, full(path).c_str(),
                  FA_WRITE | FA_CREATE_ALWAYS) != fatfs::FR_OK)
      return false;
    return fs.f_close(&f) == fatfs::FR_OK;
  }

  bool truncate(const char* path, uint64_t size) override {
    fatfs::FIL* f = openFile(path, true);
    if (f == nullptr) return false;
    uint64_t current = fs.f_size(f);
    bool ok;
    if (size <= current) {
      ok = seek(size) && fs.f_truncate(f) == fatfs::FR_OK;
    } else {
      ok = growWithZeros(current, size,
                         [&](uint64_t off, const uint8_t* b, size_t n) {
                           return write(path, off, b, n);
                         });
    }
    return ok && fs.f_sync(f) == fatfs::FR_OK;
  }

  bool mkdir(const char* path) override {
    return fs.f_mkdir(full(path).c_str()) == fatfs::FR_OK;
  }
  /// FatFs only removes empty directories
  bool rmdir(const char* path) override {
    return fs.f_unlink(full(path).c_str()) == fatfs::FR_OK;
  }
  bool remove(const char* path) override {
    closeFile();  // FatFs must not delete open files
    return fs.f_unlink(full(path).c_str()) == fatfs::FR_OK;
  }
  bool rename(const char* from, const char* to) override {
    closeFile();
    return fs.f_rename(full(from).c_str(), full(to).c_str()) == fatfs::FR_OK;
  }

  void close(const char* path) override {
    if (filePath == path) closeFile();
  }
  void flush(const char* path) override {
    if (isOpen && filePath == path) fs.f_sync(&file);
  }

  bool space(uint64_t& total, uint64_t& free) override {
    DWORD freeClusters = 0;
    fatfs::FATFS* vol = nullptr;
    if (fs.f_getfree(drive.c_str(), &freeClusters, &vol) != fatfs::FR_OK ||
        vol == nullptr)
      return false;
#if FF_MAX_SS != FF_MIN_SS
    uint64_t clusterSize = (uint64_t)vol->csize * vol->ssize;
#else
    uint64_t clusterSize = (uint64_t)vol->csize * FF_MAX_SS;
#endif
    total = (uint64_t)(vol->n_fatent - 2) * clusterSize;
    free = (uint64_t)freeClusters * clusterSize;
    return true;
  }

 protected:
  fatfs::FatFs& fs;
  std::string drive;
  fatfs::FIL file;
  std::string filePath;
  bool isOpen = false;
  bool fileWritable = false;

  std::string full(const char* path) const { return drive + path; }

  fatfs::FIL* openFile(const char* path, bool writable) {
    if (isOpen && filePath == path && (fileWritable || !writable))
      return &file;
    closeFile();
    BYTE mode = writable ? FA_READ | FA_WRITE
                                : FA_READ;
    // fails for directories
    if (fs.f_open(&file, full(path).c_str(), mode) != fatfs::FR_OK)
      return nullptr;
    isOpen = true;
    filePath = path;
    fileWritable = writable;
    return &file;
  }

  void closeFile() {
    if (isOpen) fs.f_close(&file);
    isOpen = false;
    filePath.clear();
  }

  /// Positions the open file: in read mode FatFs stops at the end of file
  bool seek(uint64_t offset) {
    if (fs.f_tell(&file) == offset) return true;
    return fs.f_lseek(&file, (FSIZE_t)offset) == fatfs::FR_OK;
  }

  int64_t writeAll(const uint8_t* buffer, size_t len) {
    UINT n = 0;
    if (fs.f_write(&file, buffer, (UINT)len, &n) != fatfs::FR_OK)
      return -1;
    return n == len ? (int64_t)n : -1;  // n < len: disk full
  }

  static void toInfo(const fatfs::FILINFO& fi, FileInfo& info) {
    info.isDirectory = (fi.fattrib & AM_DIR) != 0;
    info.size = info.isDirectory ? 0 : (uint64_t)fi.fsize;
    info.modified = toUnix(fi.fdate, fi.ftime);
  }

  /// Converts the FAT date and time (local time) to unix time
  static uint64_t toUnix(uint16_t date, uint16_t time) {
    if (date == 0) return 0;
    int y = 1980 + (date >> 9);
    unsigned m = (date >> 5) & 0x0F;
    unsigned d = date & 0x1F;
    if (m < 1 || m > 12 || d < 1) return 0;
    // days since 1970-01-01 (Howard Hinnant's days_from_civil)
    y -= m <= 2;
    int era = y / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    return (uint64_t)(days * 86400 + (time >> 11) * 3600 +
                      ((time >> 5) & 0x3F) * 60 + (time & 0x1F) * 2);
  }
};

}  // namespace smb
