# Running on the Desktop

The library also runs on Linux and macOS on top of the [Arduino Emulator](https://github.com/pschatzmann/Arduino-Emulator), which provides the Arduino API (including `WiFiServer`/`WiFiClient` and an emulated `SD` library) on the desktop. This is useful to:

- develop and debug sketches without flashing an ESP32,
- run the tests,
- share a local directory or access a share from the PC.

## Requirements

- CMake 3.16 or newer and a C++17 compiler
- git: CMake downloads the Arduino Emulator automatically
- Optional, for the end-to-end test: `smbclient` (Debian/Ubuntu package `smbclient`)
- Optional, for the Samba test: Docker

## Building

```bash
cmake -B build
cmake --build build
```

To use a local checkout of the emulator instead of downloading it:

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=/path/to/Arduino-Emulator
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `SMB_BUILD_EXAMPLES` | `ON` | Build the desktop examples |
| `SMB_BUILD_TESTS` | `ON` | Build the tests and register them with ctest |
| `SMB_TEST_SAMBA` | `OFF` | Also test the client against Samba in a Docker container |

## Running the examples

The server examples listen on port 4450, because port 445 needs root permissions.

```bash
SMB_ROOT=$HOME/Public ./build/examples/desktop/smb-server-desktop   # FileSystemPosix, share "share"
./build/examples/desktop/smb-server-sd-desktop                      # emulated SD library, current directory, share "sd"
./build/examples/desktop/smb-client-desktop    # lists \\127.0.0.1\share (port 4450) and writes a file
```

Connect to a server example from the same machine:

```bash
smbclient //localhost/share -p 4450 -U user%password
sudo mount -t cifs //localhost/share /mnt -o port=4450,username=user,password=password
```

## Running the tests

```bash
cd build
ctest --output-on-failure
```

- `smb-crypto` checks MD4, MD5, SHA-256, HMAC, RC4 and the NTLMv2 password hash against published test vectors.
- `smb-smbclient` runs [tests/smbclient-test.sh](../tests/smbclient-test.sh). It starts the test server [tests/smb-test/smb-test.ino](../tests/smb-test/smb-test.ino) and checks with `smbclient`: share enumeration, listing, upload and download of a 3 MB file, mkdir, rename, delete, read-only shares, wrong passwords, guest logins, unknown shares, signing and the 2.0.2 dialect. It runs on a `FileSystemPosix`, on an emulated SD share and on a `FileSystemFatFs` share in an arduino-fatfs disk image, which is checked by downloading the files again. Read-only users are tested as well.
- `smb-client` runs [tests/client-test.sh](../tests/client-test.sh). It starts the same test server and checks the `SMBClient` with [tests/smb-client-test/smb-client-test.ino](../tests/smb-client-test/smb-client-test.ino): buffered, byte-wise and large reads and writes, seek, append, truncate, stat, directories, rename, delete, free space, reconnect, signing, read-only shares and wrong passwords. The results are compared with the files on the local disk; on the FatFs share, which also tests timestamps and the rename of directories, they are read back with the client.

- `smb-client-samba` runs [tests/samba-test.sh](../tests/samba-test.sh). It starts Samba in a Docker container (`alpine` with the `samba` package) on port 4451 and runs the client test against it. This also covers the NTLM MIC and SPNEGO `mechListMIC`, which the arduino-smb server doesn't use. The test needs Docker and internet access and takes a few minutes, so it is only registered with `-DSMB_TEST_SAMBA=ON`.

- `smb-client-pending` runs [tests/pending-test/pending-test.cpp](../tests/pending-test/pending-test.cpp). A scripted mock server answers CREATE first with an interim `STATUS_PENDING` response and sends the signed final response later. It checks that the client waits for it, gives up after the pending timeout and rejects an invalid signature.

If smbclient isn't installed, the `smb-smbclient` test isn't registered and CMake prints a message.

The test server downloads [arduino-fatfs](https://github.com/pschatzmann/arduino-fatfs) with CMake. To use a local checkout, configure with `-DFETCHCONTENT_SOURCE_DIR_ARDUINO_FATFS=/path/to/arduino-fatfs`.

## Using the library in your own CMake project

```cmake
add_subdirectory(path/to/arduino-smb)

add_executable(my-server my-server.cpp)
target_link_libraries(my-server PRIVATE arduino-smb)
```

The `arduino-smb` target only provides headers. Linking against it adds the include path, the `IS_DESKTOP` and `USE_FILESYSTEM` definitions and the `arduino_emulator` library. If your project already defines an `arduino_emulator` target, that one is used and nothing is downloaded.

## Differences from the ESP32 build

- `IS_DESKTOP` is defined. It sets the client timeout to 0 when a client connects; otherwise the emulator's `available()` waits up to 200 ms for data, which slows down every request.
- `SDMMCFileSystem`, `FileSystemFS` and `SMBFS` aren't available. On the desktop, `SDCardFileSystem` uses the emulated SD library.
- Include `WiFi.h` before `SMB_SD.h`: the emulated `SD.h` has a `using namespace std`, which otherwise makes `byte` ambiguous.
- The desktop examples in `examples/desktop/` also appear in the Arduino IDE's example list, but they only compile on the desktop.
