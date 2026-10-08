# Server Configuration

Call these methods on `SMBServer` before `begin()`. Users can also be changed while the server is running. The client configuration is described in [client.md](client.md#configuration).

| Method | Default | Description |
|---|---|---|
| `addShare(name, fs, root, readOnly, comment)` | root `"/"`, writable | Exports the directory `root` of the file system `fs` as share `name` |
| `addUser(name, password, readOnly)` | none, read/write | Adds a user for the NTLMv2 login. User names aren't case-sensitive |
| `addReadOnlyUser(name, password)` | none | Adds a user who can only read: creating, changing, renaming and deleting files is refused on all shares |
| `setPassword(name, password)` | | Changes the password of a user; false if the user doesn't exist |
| `removeUser(name)` | | Removes a user and closes its sessions; false if the user doesn't exist |
| `setGuestAccess(allow)` | enabled only if no user is defined | Allows anonymous logins and logins with unknown user names |
| `setServerName(name, workgroup)` | `"ARDUINO"`, `"WORKGROUP"` | Names reported to the clients |
| `setSigningRequired(required)` | `false` | Requires signed messages. Guests can't sign, so this needs a user login |
| `setMaxClients(count)` | 4 | Maximum number of concurrent connections |
| `setMaxIOSize(bytes)` | automatic | Maximum read/write size per request (see below) |
| `setTimingLog(active)` | `false` | Logs where the time goes when a file that was read or written is closed (see [notes.md](notes.md#timing-log)) |

Several shares can use the same file system:

```cpp
smbServer.addShare("music", sdFiles, "/music", true, "Music (read-only)");
smbServer.addShare("upload", sdFiles, "/upload");
```

## Users

A user has read/write access unless it is defined as read-only. A read-only user can open and download files and list directories, but every create, write, truncate, rename and delete is refused with `STATUS_MEDIA_WRITE_PROTECTED` (`STATUS_ACCESS_DENIED` for delete requests). A writable share is read-only for a read-only user; a read-only share is read-only for every user.

```cpp
smbServer.addUser("admin", "password");          // read/write
smbServer.addReadOnlyUser("guest", "secret");    // read only
// same as: smbServer.addUser("guest", "secret", true);
```

Guest logins keep the access rights of the share.

### Changing users at runtime

`setPassword()` and `removeUser()` can be called at any time, e.g. from `loop()`:

- After `setPassword()`, new logins need the new password. Clients which are already logged in stay connected.
- `removeUser()` closes all sessions of the user immediately; the clients get an error with their next request and can't log in again. If guest access is enabled, the removed user name is treated like any unknown name and gets a guest login.

The changes are only kept in memory: the sketch must store them itself (e.g. in a file or `Preferences`) and add the users again after a restart.

SMB clients can't change passwords (Windows and `smbpasswd` use the SAMR service for that, which the server doesn't provide).

## Signing

Messages are signed when the client or the server requires it. Signing uses HMAC-SHA256 over every message, so it costs some CPU time. Guest sessions have no session key and are never signed.

## Buffer size

`setMaxIOSize()` sets the largest read or write request. The server needs a receive buffer per connection and a shared response buffer of about this size. Both buffers are only allocated while a large request is processed.

With the automatic setting, the size is 64 KB. On an ESP32 without PSRAM it is 32 KB. Lower values save memory but reduce the throughput.

## Logging

```cpp
SMBLogger.begin(Serial, SMBLogLevel::Info);   // None, Error, Info, Debug
```

Logging is off until you call `begin()`.

## Namespace

`SMB.h` adds `using namespace smb`. Define `SMB_NO_USING_NAMESPACE` before including it to prevent this, and use the `smb::` prefix instead.
