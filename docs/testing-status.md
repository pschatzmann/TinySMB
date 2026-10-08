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

Still to be done:

- [ ] Server with the Linux kernel client (`mount -t cifs`)
- [ ] Server on a real ESP32 with SD_MMC: throughput, and the 32 KB IO size on boards without PSRAM
- [ ] Client on a real ESP32 with `SMBFS`
- [ ] Server with macOS Finder (`smb://ip/share`)
- [ ] Client with a macOS share
- [ ] Client with a real `STATUS_PENDING`: open a file on Samba which another client holds with an oplock, so that Samba must break the oplock first. The client logic is covered by the mock server test

## Help wanted: Windows

Tests with Windows are out of scope, because no Windows machine is available. Please report your results (working or not) as a GitHub issue, together with the Windows version:

- Server with Windows 10/11: Explorer (`\\ip\share`), `net use`, copying large files, signing (Windows 11 24H2)
- Client with a Windows share
