# Tiny Server Message Block (SMB) File Sharing Library

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg?logo=cmake)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

SMB (Server Message Block) is the network file sharing protocol of Windows. It is also supported by macOS, Linux, Android and iOS. A share appears as a network drive or folder, and clients can browse, open, copy, rename and delete the files on it.

This header-only library provides an SMB2 **server** and an SMB2 **client** for Arduino. Both use only the Arduino networking API (`Server`/`Client`), so they work with `WiFiServer`/`WiFiClient`, `EthernetServer`/`EthernetClient` and others. The main target is the ESP32.

| Component | What it does | Documentation |
|---|---|---|
| `SMBServer` | Shares an SD card (SD or SD_MMC), flash file system or directory with Windows, macOS and Linux | [docs/server.md](docs/server.md) |
| `SMBClient` | Reads and writes the files on a Windows, macOS or Samba share; on the ESP32 also as `fs::FS` (`SMBFS`) | [docs/client.md](docs/client.md) |

Both implement the SMB 2.0.2 and 2.1 dialects with NTLMv2 authentication and message signing.

## Documentation

- [Server](docs/server.md): features, file systems and an example.
  - [Connecting to the Server](docs/server-connect.md): Windows, macOS and Linux, including mounting and troubleshooting.
  - [Server Configuration](docs/server-configuration.md): users, guest access, shares, signing, limits and logging.
  - [Server File Systems](docs/server-file-systems.md): the storage adapters, their limitations and how to write your own.
- [Client](docs/client.md): API, ESP32 `fs::FS` integration, configuration and an example.
- [Usage Notes](docs/notes.md): supported protocol features, security, performance and memory. Read this before using the library.
- [Running on the Desktop](docs/desktop.md): CMake build with the Arduino Emulator, desktop examples and tests.
- [Testing Status](docs/testing-status.md): what is tested, open tests and how to report results.


## Installation

For Arduino, you can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-smb.git
```

The library is header-only and has no dependencies besides the Arduino core. Include `SMB.h`, or `SMB_SD.h` / `SMB_SDMMC.h` to share an SD card.


