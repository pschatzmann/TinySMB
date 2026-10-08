# SMB Client

`SMBClient` connects to a share on a Windows PC, a Mac, a NAS, a Samba server or an arduino-smb server, and reads and writes its files. It works with any Arduino `Client` (`WiFiClient`, `EthernetClient` …).

## Features

- SMB 2.0.2 and 2.1 dialects
- NTLMv2 user authentication including the MIC that Windows and Samba ask for; anonymous logins with an empty user
- Message signing (used when the server requires it, or with `setSigningRequired(true)`)
- Files with the Arduino `File` API: buffered `read()`/`write()`, `seek()`, `truncate()`, append mode
- Directory listing, mkdir, rmdir, remove, rename, exists, stat and free space
- Automatic reconnect when the connection was lost
- ESP32: `SMBFS` provides the share as `fs::FS`, so it can be used like SD or LittleFS

## Example

```cpp
#include <WiFi.h>
#include "SMB.h"

WiFiClient wifiClient;
SMBClient smbClient(wifiClient);

void setup() {
  Serial.begin(115200);
  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);

  // \\192.168.1.10\share
  smbClient.begin("192.168.1.10", "share", "user", "password");

  SMBFile file = smbClient.open("/hello.txt", "w");
  file.println("Hello");
  file.close();

  smbClient.listDir("/", [](const FileInfo& info) {
    Serial.println(info.name.c_str());
    return true;  // continue
  });
}

void loop() {}
```

Examples:

- [smb-client-fs](../examples/smb-client-fs/smb-client-fs.ino): ESP32 with `SMBFS`
- [smb-client-desktop](../examples/desktop/smb-client-desktop/smb-client-desktop.ino): desktop version, see [desktop.md](desktop.md)

## SMBClient

| Method | Description |
|---|---|
| `begin(host, share, user, password, domain, port)` | Connects to `\\host\share`. `domain` defaults to `""` and `port` to 445. An empty user logs in anonymously |
| `end()` | Disconnects |
| `isConnected()` | Checks the connection |
| `lastStatus()` | NT status code of the last request, e.g. `STATUS_LOGON_FAILURE` |
| `open(path, mode)` | Opens a file or directory and returns an `SMBFile` |
| `exists(path)`, `stat(path, info)` | Checks a path, provides its `FileInfo` (name, isDirectory, size, modified) |
| `listDir(path, callback)` | Calls the callback for each entry; return `false` to stop |
| `mkdir(path)`, `rmdir(path)`, `remove(path)` | Creates or deletes directories and files. `rmdir()` fails for a directory which is not empty |
| `rename(from, to, replace)` | Renames or moves a file or directory |
| `space(total, free)` | Size and free space of the share in bytes |

Paths start with `/` and use `/` as separator. They are relative to the share.

Open modes:

| Mode | Description |
|---|---|
| `"r"` | Read; also opens directories |
| `"r+"` | Read and write an existing file |
| `"w"`, `"w+"` | Create or truncate the file, then read and write |
| `"a"`, `"a+"` | Create the file if needed; writes append at the end |

## SMBFile

`SMBFile` is a `Stream` with the methods of the Arduino `File` class: `read()`, `write()`, `print()`, `available()`, `peek()`, `seek()`, `position()`, `size()`, `flush()`, `close()`, `name()`, `path()`, `isDirectory()`, `getLastWrite()`, `openNextFile()` and `rewindDirectory()`. In addition:

- `truncate(size)` changes the file size.
- `getNextEntry(info)` provides the next directory entry without opening it. This is faster than `openNextFile()`, which needs a request per file.
- `setBufferSize(size)` changes the read and write buffer (default 4096 bytes). Reads and writes that are larger than the buffer are sent directly.

Copies of an `SMBFile` refer to the same open file. It is closed with `close()` or when the last copy is destroyed. The `SMBClient` must outlive its files. After a reconnect, the files of the old connection are no longer valid.

## ESP32: SMBFS

`SMBFS` provides the share as ESP32 file system. Code which works with SD or LittleFS also works with a share:

```cpp
SMBFS smbFS(smbClient);

File file = smbFS.open("/log.txt", FILE_APPEND);
file.println("entry");
file.close();
```

`smbFS.open(path, mode, true)` creates missing parent directories.

## Configuration

Call these methods before `begin()`.

| Method | Default | Description |
|---|---|---|
| `setMaxIOSize(bytes)` | automatic | Largest read or write request: 64 KB, 32 KB on an ESP32 without PSRAM. The server can reduce it |
| `setTimeout(ms)` | 10000 | Timeout for a response |
| `setPendingTimeout(ms)` | 60000 | Timeout for the final response after the server reported that a request is pending. Servers do this e.g. while another client must release a file it has cached (Windows waits up to 35 s) |
| `setSigningRequired(required)` | `false` | Signs all messages even if the server doesn't require it |

## Limitations

- Servers which only allow SMB 3 or which require encryption are not supported. This includes Azure Files and shares configured with "encrypt data". Windows, macOS, Samba and most NAS devices still allow SMB 2.1.
- Kerberos is not supported: domain accounts log in with NTLMv2.
- One request at a time: the client waits for each response.
- The client is tested against the arduino-smb server and Samba 4.23. It is not tested with Windows and macOS shares yet; please report your results as a GitHub issue, see [testing-status.md](testing-status.md).
