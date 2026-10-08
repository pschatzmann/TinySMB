# Usage Notes

## Protocol support

The server and the client implement the SMB2 dialects 2.0.2 and 2.1. The server uses SMB 1 only to switch a client to SMB2. SMB 3 isn't supported. All current clients and servers still support SMB 2.1, unless they are configured to require SMB 3.

Not supported:

- Encryption (an SMB 3 feature): data is transferred unencrypted
- Kerberos: logins use NTLMv2 (NTLMv1 is rejected)
- Oplocks and leases: clients don't cache file data
- Change notifications: Explorer doesn't refresh automatically when files change on the server (press F5)
- Alternate data streams, extended attributes, file locking (lock requests succeed but have no effect) and setting timestamps or attributes
- Server side copy: clients copy files by reading and writing them

The client sends one request at a time. See [client.md](client.md#limitations).

## Security

- Passwords aren't transferred, but data is sent unencrypted. Only use SMB on trusted networks.
- Prefer user logins to guest access: guest sessions can't be signed, so the data can be changed on its way.
- Users have full access to all writable shares unless they are added with `addReadOnlyUser()`; use read-only shares or read-only users to protect data.
- The passwords of the server users and of the client are stored in plain text in the sketch.

## SD cards

Don't change the files on the card from the sketch while clients are connected to the server. The clients don't notice the changes and may show outdated data.

## Performance

- `WiFi.setSleep(false)` improves the throughput a lot. With WiFi power saving enabled, every request can wait for the next WiFi wake-up.
- The throughput is limited by WiFi and by the SD card. Larger buffers (`setMaxIOSize()`) help, so use an ESP32 with PSRAM if possible.
- Call `smbServer.loop()` often and avoid long `delay()` calls in `loop()`: each request waits until `loop()` runs.
- Client: read and write in large blocks, or increase `SMBFile::setBufferSize()`. Each request needs a round trip to the server.

## Timing log

`smbServer.setTimingLog(true)` shows where the time goes. When a file that was read or written is closed, the server logs (level Info) a summary:

```
[SMB] I: timing /big.bin: 46 reads, 0 writes, 3000000 bytes in 148 ms (20270 KB/s)
[SMB] I: timing   storage 2 ms (1%), cpu 138 ms (93%), send 5 ms (3%), receive 0 ms (0%), wait 1 ms (0%)
```

| Part | Meaning |
|---|---|
| storage | `FileSystem` reads and writes, plus the final close (which flushes written data) |
| cpu | the rest of the request processing: parsing, copying and message signing |
| send | sending the responses until the network stack accepted them |
| receive | receiving the requests (mainly the data of writes) |
| wait | the remaining time: waiting for the next request of the client, or for the next `loop()` call |

The times start with the first read or write and end when the file is closed. A high `storage` share means the SD card is the bottleneck, a high `send` or `receive` share means WiFi is. A high `cpu` share usually comes from signing: it calculates an HMAC-SHA256 over every message.

## Memory

Each connection needs a few KB plus a receive buffer of up to the maximum IO size (32 KB on an ESP32 without PSRAM, 64 KB otherwise) while large requests are processed. The response buffer of the same size is shared by all connections. Reduce `setMaxClients()` or `setMaxIOSize()` if memory is short.

The client needs a request and a response buffer of up to the maximum IO size, plus the read/write buffer of each open file (4 KB by default).
