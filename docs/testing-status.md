# Testing Status

## Automated tests

These tests run with `ctest` (see [desktop.md](desktop.md)):

| Test | Status |
|---|---|
| Server with `smbclient` (Samba client) | ✅ |
| Client with the arduino-smb server | ✅ |
| Client with Samba 4.23 (Docker, `-DSMB_TEST_SAMBA=ON`) | ✅ |
| Client with interim `STATUS_PENDING` responses (scripted mock server) | ✅ |

## Manual tests

Done:

- [x] Server on a real ESP32 with SD_MMC, connected from Linux (`mount -t cifs`/`smbclient`)
- [x] Server on a real ESP32 with the SD library (SPI, `SMB_SD.h`/`SDCardFileSystem`), connected from Linux
- [x] Server on a real ESP32 with arduino-fatfs (SPI, `SMB_FatFs.h`/`FileSystemFatFs`), connected from Linux
- [x] Server with the Linux kernel client (`mount -t cifs`), tested against the desktop `smb-test` server. This
  uncovered a real bug: the kernel client queries `FileFullEaInformation` (info class 15) while listing/reading
  files, which the server rejected with `STATUS_INVALID_INFO_CLASS`, breaking the mount. Fixed by returning an
  empty EA list for that info class. Verified mount, directory listing, file read/write/delete all work correctly
  afterwards.
- [x] Server on a real ESP32 (no PSRAM) with SD_MMC: throughput and the 32 KB IO size fallback, connected from
  Linux (`mount -t cifs`). The boot log confirmed `max io: 32768` is chosen automatically when PSRAM isn't
  present. Reading files over WiFi reached roughly 430-590 KB/s, with `send` consistently the dominant cost
  (around 65-78%) and `storage`/`cpu` both low (single digits to ~8%), so WiFi transmission is the bottleneck on
  this board, not the SD card or message signing. A ~184 MB file read completed successfully end-to-end at
  ~518 KB/s average.

  This testing also uncovered a real bug: `PsramAllocator::allocate()` (in
  [SMBAlloc.h](../src/smb-server/SMBAlloc.h)) returned `nullptr` when both the PSRAM and internal-RAM
  `heap_caps_malloc()` calls failed, which `std::vector` doesn't check for, leading to a null-pointer crash
  (`Guru Meditation: StoreProhibited`) inside `sendResponses()` under low memory. Fixed to detect out-of-memory
  and `abort()` with a diagnostic message (`printf` with the requested size and free heap/PSRAM) instead of
  silently corrupting memory, since the ESP32 Arduino core builds without C++ exceptions
  (`__cpp_exceptions == 0`). After the fix, no further crashes were observed, including on a much larger file
  than the one that originally crashed.

  Also added an interval-based progress option to `setTimingLog(active, intervalMs)` (previously it only logged
  once when a file was closed, which meant no visibility into a transfer still in progress for large/long-lived
  files).

Still to be done:

- [ ] Server on a real ESP32 with SD_MMC: write throughput (only read throughput was measured so far)
- [ ] Client on a real ESP32 with `SMBFS`
- [ ] Server with macOS Finder (`smb://ip/share`)
- [ ] Client with a macOS share
- [ ] Client with a real `STATUS_PENDING`: open a file on Samba which another client holds with an oplock, so that Samba must break the oplock first. The client logic is covered by the mock server test

## Help wanted: Windows

Tests with Windows are out of scope, because no Windows machine is available. Please report your results (working or not) as a GitHub issue, together with the Windows version:

- Server with Windows 10/11: Explorer (`\\ip\share`), `net use`, copying large files, signing (Windows 11 24H2)
- Client with a Windows share
