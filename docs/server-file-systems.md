# Server File Systems

The server accesses storage through the `FileSystem` interface. All paths start with `/` and use `/` as separator.

## SD_MMC and SD on the ESP32

`SDMMCFileSystem` (`SMB_SDMMC.h`) and `SDCardFileSystem` (`SMB_SD.h`) are based on `FileSystemFS`. They also report the card size and free space to the clients.

Start the SD driver (`SD_MMC.begin()` or `SD.begin()`) before the clients connect.

Tested on a real ESP32 with the SD library over SPI (`SMB_SD.h`/`SDCardFileSystem`), connected from Linux.

On boards where the default SDMMC pins are not usable (e.g. they are shared with a camera, PSRAM/OPI flash, or other peripherals), construct `SDMMCFileSystem` with the pins for your board and call `logPinSetup()` from `setup()` (after `SMBLogger.begin()`) to confirm the assignment and the result of `SD_MMC.setPins()` in the log:

```cpp
SDMMCFileSystem sdFiles(SD_MMC, /*clk*/42, /*cmd*/39, /*d0*/41, /*d1*/40, /*d2*/37, /*d3*/38);

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  SMBLogger.begin(Serial, SMBLogLevel::Info);
  sdFiles.logPinSetup();
  SD_MMC.begin("/sdcard");
  ...
}
```

## Any ESP32 file system: `FileSystemFS`

`FileSystemFS` works with every `fs::FS`, for example LittleFS:

```cpp
#include <LittleFS.h>
#include "SMB.h"

FileSystemFS flashFiles(LittleFS);
```

To report the free space, set a callback:

```cpp
flashFiles.setSpaceCallback([](uint64_t& total, uint64_t& free) {
  total = LittleFS.totalBytes();
  free = total - LittleFS.usedBytes();
  return true;
});
```

For file systems mounted on a VFS mountpoint (SD, SD_MMC, FFat), `listDir()` uses the POSIX `opendir()`/`readdir()`/`stat()` API directly instead of the Arduino `fs::File`/`openNextFile()` API. This avoids the overhead of constructing a `File` object per entry and is noticeably faster for directories with many files or subdirectories. File systems without a VFS mountpoint (e.g. LittleFS/SPIFFS in some configurations) automatically fall back to the `fs::File` based listing.

The server also only lists a directory once per open and caches the (pattern matched) entries in memory, instead of re-scanning storage for every SMB page of a large directory listing.

## Arduino SD API: `FileSystemSD`

`FileSystemSD<SDT, FileT>` works with libraries that follow the Arduino SD API (`open(path, mode)`, `exists()`, `mkdir()`, `remove()`, `File::seek()`, `File::openNextFile()` …). On other platforms than the ESP32, `SDCardFileSystem` uses it with the standard `SD` library.

The open modes can be passed to the constructor if a library uses different flags. Include the library's header before `SMB.h` so that the default modes are found.

The Arduino SD API has a few gaps:

- There are no timestamps: all files show 1980-01-01.
- There is no rename: files are renamed by copying, which is slow for large files. Directories can't be renamed.
- Files can't be shrunk, except to size 0.

## TinyFATFS: `FileSystemFatFs`

`FileSystemFatFs` (`SMB_FatFs.h`) uses the FatFs API of the [TinyFATFS](https://github.com/pschatzmann/TinyFATFS) library. It works with every arduino-fatfs driver: SD cards via __SPI or SDMMC, RAM disks, disk images on the desktop__ … Unlike the Arduino SD API, it supports timestamps, the rename of files and directories, shrinking files and the free space.

Tested on a real ESP32 with a SD card over SPI, connected from Linux.

```cpp
#include <WiFi.h>
#include "SMB_FatFs.h"

FileSystemFatFs fatFiles(*SD.getFatFs());  // SD: arduino-fatfs SDClass
// in setup(): SD.begin(SS); then smbServer.addShare("sd", fatFiles);
```

Notes:

- Include `SMB_FatFs.h` instead of `SMB.h`, and don't include the Arduino `SD.h` as well: both libraries define `SD` and `File`.
- A volume that isn't mounted as drive 0 needs the drive prefix: `FileSystemFatFs fat(fatFs, "1:")`.
- New and changed files only get the current time if `FF_FS_NORTC` is 0 in `ffconf.h` and the sketch provides `get_fattime()`. Otherwise FatFs uses the fixed date `FF_NORTC_YEAR`-`FF_NORTC_MON`-`FF_NORTC_MDAY`.
- File names are converted with the code page `FF_CODE_PAGE` (437 by default). Set `FF_LFN_UNICODE` to 2 (UTF-8) to support all characters.

## POSIX: `FileSystemPosix`

`FileSystemPosix(root)` exports a local directory with the POSIX API. It is used on the desktop. On the ESP32 it also works with a VFS mount point, e.g. `FileSystemPosix("/sdcard")` after `SD_MMC.begin()`.

## Open files

Clients keep many files open at the same time, while FAT on the ESP32 only allows a few open files (`max_files`, 5 by default). So the adapters don't keep a file open per client handle: they keep only the file which was used last, and reopen files as needed. This is fast for the usual case of reading or writing one file sequentially.

## Writing your own adapter

Subclass `FileSystem` and implement:

| Method | Description |
|---|---|
| `stat(path, info)` | Fills `FileInfo` (name, isDirectory, size, modified); false if the path doesn't exist |
| `listDir(path, callback)` | Calls the callback for each entry; stop when it returns false |
| `read(path, offset, buffer, len)` / `write(...)` | Byte counts, -1 on error. Writing past the end must extend the file |
| `createFile(path)` | Creates an empty file, or truncates an existing one |
| `truncate(path, size)` | Changes the file size |
| `mkdir`, `rmdir`, `remove`, `rename` | Directory and file operations |

Optional: `close(path)` and `flush(path)` to release resources, `space(total, free)`, `name()` (file system name shown to clients, default `FAT32`) and `isCaseSensitive()`.
