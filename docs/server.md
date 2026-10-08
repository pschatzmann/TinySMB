# SMB Server

`SMBServer` exports storage of the microcontroller as SMB share. Windows, macOS and Linux can then use it like any other network drive.

## Features

- SMB 2.0.2 and 2.1 dialects (a client negotiates the highest dialect both sides support)
- NTLMv2 user authentication with message signing; read/write and read-only users; optional guest access
- Several shares; read-only shares; several concurrent clients
- Browse, read, write, create, delete, rename and resize files and directories
- Share enumeration, so `\\server` lists the shares in Windows Explorer

## File systems

| Class | Header | Description |
|---|---|---|
| `SDMMCFileSystem` | `SMB_SDMMC.h` | SD card using the ESP32 **SD_MMC** library |
| `SDCardFileSystem` | `SMB_SD.h` | SD card using the **SD** library (ESP32 or Arduino SD API) |
| `FileSystemFS` | `SMB.h` | Any ESP32 `fs::FS`: LittleFS, SPIFFS, FFat … |
| `FileSystemSD<SDT, FileT>` | `SMB.h` | Any library with the Arduino SD API (e.g. SdFat) |
| `FileSystemFatFs` | `SMB_FatFs.h` | Any volume of the [arduino-fatfs](https://github.com/pschatzmann/arduino-fatfs) library |
| `FileSystemPosix` | `SMB.h` | POSIX directory (desktop, or an ESP32 VFS mount point) |

To add your own storage, subclass `FileSystem`. See [server-file-systems.md](server-file-systems.md).

## Example

```cpp
#include <WiFi.h>
#include "SMB_SDMMC.h"

WiFiServer wifiServer(SMB_DEFAULT_PORT);
SMBServer<WiFiServer> smbServer(wifiServer);
SDMMCFileSystem sdFiles(SD_MMC);

void setup() {
  Serial.begin(115200);
  SMBLogger::begin(Serial, SMBLogLevel::Info);
  SD_MMC.begin();
  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  WiFi.setSleep(false);
  smbServer.addUser("user", "password");
  smbServer.addShare("sdcard", sdFiles);
  smbServer.begin();
}

void loop() { smbServer.loop(); }
```

Examples:

- [smb-server-sdmmc](../examples/smb-server-sdmmc/smb-server-sdmmc.ino): SD card with SD_MMC
- [smb-server-sdcard](../examples/smb-server-sdcard/smb-server-sdcard.ino): SD card with the SD library
- [smb-server-desktop](../examples/desktop/smb-server-desktop/smb-server-desktop.ino) and [smb-server-sd-desktop](../examples/desktop/smb-server-sd-desktop/smb-server-sd-desktop.ino): desktop versions, see [desktop.md](desktop.md)

## Connecting

```
Windows:  \\192.168.1.50\sdcard
macOS:    Finder → Go → Connect to Server → smb://192.168.1.50/sdcard
Linux:    smbclient //192.168.1.50/sdcard -U user%password
```

See [server-connect.md](server-connect.md) for mounting the share, and for client settings that affect guest access and signing.
