# Tiny Server Message Block (SMB) File Sharing Library

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![Doxygen](https://img.shields.io/badge/API-Doxygen-blue.svg)](https://pschatzmann.github.io/TinySMB/)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg?logo=cmake)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

__SMB__ (Server Message Block) is the __network file sharing protocol__ of Windows. It is also supported by macOS, Linux, Android and iOS. A share appears as a network drive or folder, and clients can browse, open, copy, rename and delete the files on it.

This __header-only C++ library__ provides an SMB2 **server** and an SMB2 **client** for Arduino. Both use only the Arduino networking API (`Server`/`Client`), so they work with `WiFiServer`/`WiFiClient`, `EthernetServer`/`EthernetClient` and others. The main target is the ESP32.

| Component | What it does | Documentation |
|---|---|---|
| [`SMBServer`](https://pschatzmann.github.io/TinySMB/classsmb_1_1_s_m_b_server.html) | Shares an SD card (SD or SD_MMC), flash file system or directory with Windows, macOS and Linux | [Server](https://github.com/pschatzmann/TinySMB/wiki/Server) |
| [`SMBClient`](https://pschatzmann.github.io/TinySMB/classsmb_1_1_s_m_b_client.html) | Reads and writes the files on a Windows, macOS or Samba share; on the ESP32 also as `fs::FS` ([`SMBFS`](https://pschatzmann.github.io/TinySMB/classsmb_1_1_s_m_b_f_s.html)) | [Client](https://github.com/pschatzmann/TinySMB/wiki/Client) |

Both implement the SMB 2.0.2 and 2.1 dialects with NTLMv2 authentication and message signing.

## Documentation

The documentation is in the [Wiki](https://github.com/pschatzmann/TinySMB/wiki); the class reference is generated with Doxygen: [API documentation]().

- [Server](https://github.com/pschatzmann/TinySMB/wiki/Server): features, file systems and an example.
  - [Connecting to the Server](https://github.com/pschatzmann/TinySMB/wiki/Server-Connect): Windows, macOS and Linux, including mounting and troubleshooting.
  - [Server Configuration](https://github.com/pschatzmann/TinySMB/wiki/Server-Configuration): users, guest access, shares, signing, limits and logging.
  - [Server File Systems](https://github.com/pschatzmann/TinySMB/wiki/Server-File-Systems): the storage adapters, their limitations and how to write your own.
- [Client](https://github.com/pschatzmann/TinySMB/wiki/Client): API, ESP32 `fs::FS` integration, configuration and an example.
- [Usage Notes](https://github.com/pschatzmann/TinySMB/wiki/Usage-Notes): supported protocol features, security, performance and memory. Read this before using the library.
- [Running on the Desktop](https://github.com/pschatzmann/TinySMB/wiki/Running-on-the-Desktop): CMake build with the Arduino Emulator, desktop examples and tests.
- [Testing Status](https://github.com/pschatzmann/TinySMB/wiki/Testing-Status): what is tested, open tests and how to report results.

## Logging

Both the server and the client use a minimal, shared logger: a ready-to-use static instance `smb::SMBLogger` of class `smb::SMBLoggerClass`, writing to any Arduino `Print` destination (e.g. `Serial`). Logging is disabled by default; call `begin()` once to activate it:

```cpp
#include "SMB.h"

void setup() {
  Serial.begin(115200);
  smb::SMBLogger.begin(Serial, smb::SMBLogLevel::Info);
  // ...
}
```

Available log levels (`smb::SMBLogLevel`), from least to most verbose:

- `None` – logging disabled (default)
- `Error` – only errors
- `Info` – errors and informational messages
- `Debug` – errors, info and detailed protocol/debug messages

Internally, the library logs with the `SMB_LOGE`, `SMB_LOGI` and `SMB_LOGD` macros, which are no-ops when the corresponding level is not active, so leaving logging off (or at `Error`) has negligible overhead. See [Server Configuration](https://github.com/pschatzmann/TinySMB/wiki/Server-Configuration) for more on server-side logging configuration.


## Installation

For Arduino, you can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-smb.git
```

The library is header-only and has no dependencies besides the Arduino core. Include `SMB.h`, or `SMB_SD.h` / `SMB_SDMMC.h` to share an SD card.


