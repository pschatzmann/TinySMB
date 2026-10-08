# Connecting to the Server

The server listens on TCP port 445 (`SMB_DEFAULT_PORT`). Windows always connects to port 445. Other clients can use a different port, which is useful on the desktop, where ports below 1024 need root permissions.

The server doesn't implement NetBIOS name resolution, so use the IP address. A host name works if you resolve it another way (DNS, or mDNS with the `ESPmDNS` library on the ESP32).

## Windows

Enter `\\192.168.1.50\sdcard` in the address bar of Explorer, or map it as a drive:

```
net use S: \\192.168.1.50\sdcard /user:user password
net use S: /delete
```

`\\192.168.1.50` lists all shares.

- **Guest access:** Windows 10 (1709 and later, Enterprise/Education) and Windows 11 refuse guest logons by default ("insecure guest logons"). Define a user with `addUser()` instead of enabling guest access.
- **Signing:** Windows 11 24H2 requires SMB signing by default. The server supports signing for user logins (but not for guests).
- **Cached credentials:** Windows keeps one login per server. If you change the user or password, disconnect with `net use \\192.168.1.50 /delete` first.

## macOS

Finder → *Go* → *Connect to Server…* (⌘K), then enter `smb://192.168.1.50/sdcard`.

Command line:

```bash
mkdir -p ~/sdcard
mount_smbfs //user:password@192.168.1.50/sdcard ~/sdcard
umount ~/sdcard
```

## Linux

With `smbclient` (package `smbclient`):

```bash
smbclient -L //192.168.1.50 -U user%password          # list shares
smbclient //192.168.1.50/sdcard -U user%password       # interactive (ls, get, put …)
smbclient //192.168.1.50/sdcard -p 4450 -U user%password -c "ls"   # other port
```

Mount with the kernel CIFS client (package `cifs-utils`):

```bash
sudo mount -t cifs //192.168.1.50/sdcard /mnt \
  -o username=user,password=password,vers=2.1,uid=$(id -u),gid=$(id -g)
sudo umount /mnt
```

Add `port=4450` for a server that doesn't use port 445. `vers=2.1` is optional: by default the client negotiates the highest version both sides support.

File managers such as Nautilus and Dolphin accept `smb://192.168.1.50/sdcard`.

## Troubleshooting

- Activate logging with `SMBLogger::begin(Serial, SMBLogLevel::Info)`. Use `SMBLogLevel::Debug` to see every request.
- `NT_STATUS_LOGON_FAILURE`: wrong user or password, or a guest login while guest access is disabled.
- `NT_STATUS_BAD_NETWORK_NAME`: the share name is wrong.
- The connection fails right after the protocol negotiation: the client only allows SMB 3. Allow SMB 2 in the client settings (for example `client min protocol = SMB2` in `smb.conf`).
