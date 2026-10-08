#pragma once
#include <Arduino.h>
#if __has_include(<Client.h>)
#include <Client.h>
#endif

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../smb-server/SMBBuffer.h"
#include "../smb-server/SMBCrypto.h"
#include "../smb-server/SMBDefs.h"
#include "../smb-server/SMBFileSystem.h"
#include "../smb-server/SMBLogger.h"
#include "../smb-server/SMBPlatform.h"
#include "SMBNtlmClient.h"

#if defined(ESP32)
#include <esp32-hal-psram.h>
#endif

namespace smb {

class SMBClient;

/// SMB2 file id
struct FileId {
  uint64_t persistent = ~0ULL;
  uint64_t volatileId = ~0ULL;
  bool isValid() const { return volatileId != ~0ULL; }
};

/**
 * @brief File or directory on a SMB share. It provides the Arduino File API:
 * read and write are buffered; copies refer to the same open file, which is
 * closed with close() or when the last copy is destroyed.
 */
class SMBFile : public Stream {
 public:
  SMBFile() = default;

  // ------------------------------------------------------------- Stream API
  int available() override {
    if (!isOpen()) return 0;
    uint64_t n = h->pos < h->size ? h->size - h->pos : 0;
    return n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n;
  }
  int read() override {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  int peek() override {
    if (!isOpen()) return -1;
    uint64_t pos = h->pos;
    int result = read();
    h->pos = pos;
    return result;
  }
  size_t read(uint8_t* buffer, size_t len);
  size_t readBytes(char* buffer, size_t len) {
    return read((uint8_t*)buffer, len);
  }
  size_t readBytes(uint8_t* buffer, size_t len) { return read(buffer, len); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, size_t len) override;
  using Print::write;
  void flush() override;

  // ---------------------------------------------------------- File API
  bool seek(uint64_t pos) {
    if (!isOpen() || h->isDir) return false;
    h->pos = pos;
    return true;
  }
  uint64_t position() const { return h ? h->pos : 0; }
  uint64_t size() const { return h ? h->size : 0; }
  /// Changes the file size
  bool truncate(uint64_t size);
  void close();
  const char* name() const { return h ? h->name.c_str() : ""; }
  const char* path() const { return h ? h->path.c_str() : ""; }
  bool isDirectory() const { return h && h->isDir; }
  /// Last modification as unix time
  time_t getLastWrite() const { return h ? (time_t)h->modified : 0; }
  /// Size of the read and write buffer (default 4096)
  void setBufferSize(size_t size) {
    if (h) h->bufferSize = size < 64 ? 64 : size;
  }
  operator bool() const { return isOpen(); }

  // ---------------------------------------------------------- Directories
  /// Provides the next directory entry without opening it
  bool getNextEntry(FileInfo& info);
  /// Opens the next file in the directory
  SMBFile openNextFile(const char* mode = "r");
  void rewindDirectory() {
    if (!h || !h->isDir) return;
    h->entries.clear();
    h->entryIdx = 0;
    h->dirStarted = false;
    h->dirEnd = false;
  }

 protected:
  friend class SMBClient;
  struct Handle {
    ~Handle();
    SMBClient* client = nullptr;
    uint32_t generation = 0;
    FileId id;
    std::string path, name;
    bool isDir = false, writable = false, append = false, open = false;
    uint64_t pos = 0, size = 0, modified = 0;
    size_t bufferSize = 4096;
    std::vector<uint8_t> rbuf, wbuf;
    uint64_t rbufOffset = 0, wbufOffset = 0;
    std::vector<FileInfo> entries;
    size_t entryIdx = 0;
    bool dirStarted = false, dirEnd = false;
    bool flushWrite();
    void close();
  };
  std::shared_ptr<Handle> h;
  bool isOpen() const;
};

/**
 * @brief SMB2 (dialect 2.0.2 and 2.1) client which connects to a share of a
 * Windows, macOS, Samba or arduino-smb server. It works with any Arduino
 * Client (WiFiClient, EthernetClient...).
 */
class SMBClient {
 public:
  explicit SMBClient(Client& client) : client(client) {}
  ~SMBClient() { end(); }

  /// Max read/write size per request (0 = automatic)
  void setMaxIOSize(uint32_t size) { maxIO = size; }
  /// Timeout in ms for the responses (default 10000)
  void setTimeout(uint32_t ms) { timeout = ms; }
  /// Timeout in ms for the final response after the server reported that
  /// the request is pending, e.g. while it waits for another client to give
  /// up its oplock (default 60000)
  void setPendingTimeout(uint32_t ms) { pendingTimeout = ms; }
  /// Requires signed messages even if the server doesn't
  void setSigningRequired(bool required) { signingRequired = required; }

  /// Connects to \\host\share. An empty user logs in anonymously
  bool begin(const char* host, const char* share, const char* user = "",
             const char* password = "", const char* domain = "",
             uint16_t port = SMB_DEFAULT_PORT) {
    this->host = host;
    this->share = share;
    this->port = port;
    ntlm.setCredentials(user, password, domain);
    return connect();
  }

  /// Disconnects from the server
  void end() {
    if (connectedFlag && client.connected()) {
      std::vector<uint8_t> resp;
      startRequest(SMB2_TREE_DISCONNECT);
      bodyU16(4);
      transact(resp);
      startRequest(SMB2_LOGOFF);
      bodyU16(4);
      transact(resp);
    }
    client.stop();
    connectedFlag = false;
    generation++;
  }

  /// Checks if we are connected
  bool isConnected() { return connectedFlag && client.connected(); }

  /// NT status of the last request
  uint32_t lastStatus() const { return status; }

  // ------------------------------------------------------- file operations

  /// Opens a file or directory: mode "r", "r+", "w", "w+", "a" or "a+"
  SMBFile open(const char* path, const char* mode = "r") {
    SMBFile file;
    if (!ensureConnected()) return file;
    std::string m = mode ? mode : "r";
    bool plus = m.find('+') != std::string::npos;
    uint32_t disposition = FILE_OPEN;
    bool writable = plus;
    if (m[0] == 'w') {
      disposition = FILE_OVERWRITE_IF;
      writable = true;
    } else if (m[0] == 'a') {
      disposition = FILE_OPEN_IF;
      writable = true;
    }
    uint32_t access = writable ? ACCESS_READ_WRITE : ACCESS_READ;
    FileId id;
    FileInfo info;
    if (!create(path, access, disposition, 0, id, info)) {
      // directories can not be opened for writing
      if (writable || !create(path, ACCESS_LIST, FILE_OPEN,
                              FILE_DIRECTORY_FILE, id, info))
        return file;
    }
    auto h = std::make_shared<SMBFile::Handle>();
    h->client = this;
    h->generation = generation;
    h->id = id;
    h->open = true;
    h->path = normalize(path);
    h->name = info.name;
    h->isDir = info.isDirectory;
    h->writable = writable;
    h->append = m[0] == 'a';
    h->size = info.size;
    h->modified = info.modified;
    h->pos = h->append ? h->size : 0;
    file.h = h;
    return file;
  }

  /// Provides the information for a path
  bool stat(const char* path, FileInfo& info) {
    if (!ensureConnected()) return false;
    FileId id;
    if (!create(path, ACCESS_ATTRIBUTES, FILE_OPEN, 0, id, info)) return false;
    closeFile(id);
    return true;
  }

  bool exists(const char* path) {
    FileInfo info;
    return stat(path, info);
  }

  /// Lists a directory: the callback returns false to stop
  bool listDir(const char* path,
               std::function<bool(const FileInfo&)> callback) {
    SMBFile dir = open(path, "r");
    if (!dir || !dir.isDirectory()) return false;
    FileInfo info;
    while (dir.getNextEntry(info)) {
      if (!callback(info)) break;
    }
    return true;
  }

  bool mkdir(const char* path) {
    if (!ensureConnected()) return false;
    FileId id;
    FileInfo info;
    if (!create(path, ACCESS_LIST, FILE_CREATE, FILE_DIRECTORY_FILE, id, info))
      return false;
    closeFile(id);
    return true;
  }

  bool remove(const char* path) { return deletePath(path, false); }
  bool rmdir(const char* path) { return deletePath(path, true); }

  /// Renames or moves a file or directory
  bool rename(const char* from, const char* to, bool replace = false) {
    if (!ensureConnected()) return false;
    FileId id;
    FileInfo info;
    if (!create(from, ACCESS_DELETE, FILE_OPEN, 0, id, info)) return false;
    std::vector<uint8_t> data;
    Writer w(data);
    w.u8(replace ? 1 : 0);
    w.zeros(7);
    w.u64(0);
    size_t lenPos = w.pos();
    w.u32(0);
    w.put32(lenPos, (uint32_t)w.utf16(toSmbName(to)));
    bool ok = setInfo(id, FileRenameInformation, data);
    closeFile(id);
    return ok;
  }

  /// Provides the total and free space of the share in bytes
  bool space(uint64_t& total, uint64_t& free) {
    if (!ensureConnected()) return false;
    FileId id;
    FileInfo info;
    if (!create("/", ACCESS_LIST, FILE_OPEN, FILE_DIRECTORY_FILE, id, info))
      return false;
    startRequest(SMB2_QUERY_INFO);
    Writer w(req);
    w.u16(41);
    w.u8(SMB2_0_INFO_FILESYSTEM);
    w.u8(FileFsFullSizeInformation);
    w.u32(64);  // output buffer length
    w.u16(0);
    w.u16(0);
    w.u32(0);
    w.u32(0);
    w.u32(0);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    std::vector<uint8_t> resp;
    bool ok = transact(resp) == STATUS_SUCCESS;
    if (ok) {
      Reader r(resp.data(), resp.size());
      size_t off = r.u16(66);
      uint64_t units = r.u64(off);
      uint64_t avail = r.u64(off + 8);
      uint64_t unitSize = (uint64_t)r.u32(off + 24) * r.u32(off + 28);
      total = units * unitSize;
      free = avail * unitSize;
      ok = !r.hasError();
    }
    closeFile(id);
    return ok;
  }

  // ----------------------------------------------- low level file access

  /// Opens a file (SMB2 CREATE) and provides its information
  bool create(const char* path, uint32_t access, uint32_t disposition,
              uint32_t options, FileId& id, FileInfo& info) {
    std::string name = toSmbName(path);
    startRequest(SMB2_CREATE);
    Writer w(req);
    w.u16(57);
    w.u8(0);
    w.u8(0);  // no oplock
    w.u32(2);  // impersonation
    w.u64(0);
    w.u64(0);
    w.u32(access);
    w.u32(0);  // attributes
    w.u32(7);  // share read, write, delete
    w.u32(disposition);
    w.u32(options);
    size_t nameOffPos = w.pos();
    w.u16(0);
    size_t nameLenPos = w.pos();
    w.u16(0);
    w.u32(0);  // create contexts
    w.u32(0);
    w.put16(nameOffPos, (uint16_t)w.pos());
    size_t n = w.utf16(name);
    w.put16(nameLenPos, (uint16_t)n);
    if (n == 0) w.u8(0);
    std::vector<uint8_t> resp;
    if (transact(resp) != STATUS_SUCCESS) return false;
    Reader r(resp.data(), resp.size());
    info.modified = fromFileTime(r.u64(88));
    info.size = r.u64(112);
    info.isDirectory = r.u32(120) & FILE_ATTRIBUTE_DIRECTORY;
    id.persistent = r.u64(128);
    id.volatileId = r.u64(136);
    std::string p = normalize(path);
    info.name = p == "/" ? "/" : p.substr(p.find_last_of('/') + 1);
    return !r.hasError();
  }

  bool closeFile(const FileId& id) {
    startRequest(SMB2_CLOSE);
    Writer w(req);
    w.u16(24);
    w.u16(0);
    w.u32(0);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    std::vector<uint8_t> resp;
    return transact(resp) == STATUS_SUCCESS;
  }

  /// Reads up to len bytes (limited by the max read size); -1 on error
  int64_t readFile(const FileId& id, uint64_t offset, uint8_t* buffer,
                   size_t len) {
    if (len > maxRead) len = maxRead;
    startRequest(SMB2_READ);
    Writer w(req);
    w.u16(49);
    w.u8(0x50);
    w.u8(0);
    w.u32((uint32_t)len);
    w.u64(offset);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    w.u32(0);  // minimum count
    w.u32(0);  // channel
    w.u32(0);  // remaining
    w.u16(0);
    w.u16(0);
    w.u8(0);
    std::vector<uint8_t>& resp = rx;
    uint32_t rc = transact(resp);
    if (rc == STATUS_END_OF_FILE) return 0;
    if (rc != STATUS_SUCCESS) return -1;
    Reader r(resp.data(), resp.size());
    uint32_t n = r.u32(68);
    const uint8_t* data = r.ptr(r.u8(66), n);
    if (data == nullptr || n > len) return -1;
    memcpy(buffer, data, n);
    return n;
  }

  /// Writes up to len bytes (limited by the max write size); -1 on error
  int64_t writeFile(const FileId& id, uint64_t offset, const uint8_t* buffer,
                    size_t len) {
    if (len > maxWrite) len = maxWrite;
    startRequest(SMB2_WRITE);
    Writer w(req);
    w.u16(49);
    w.u16(SMB2_HEADER_SIZE + 48);
    w.u32((uint32_t)len);
    w.u64(offset);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    w.u32(0);
    w.u32(0);
    w.u16(0);
    w.u16(0);
    w.u32(0);
    w.bytes(buffer, len);
    std::vector<uint8_t> resp;
    if (transact(resp) != STATUS_SUCCESS) return -1;
    return Reader(resp.data(), resp.size()).u32(68);
  }

  bool setFileSize(const FileId& id, uint64_t size) {
    std::vector<uint8_t> data;
    Writer(data).u64(size);
    return setInfo(id, FileEndOfFileInformation, data);
  }

  bool flushFile(const FileId& id) {
    startRequest(SMB2_FLUSH);
    Writer w(req);
    w.u16(24);
    w.u16(0);
    w.u32(0);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    std::vector<uint8_t> resp;
    return transact(resp) == STATUS_SUCCESS;
  }

  /// Provides the next batch of directory entries; false at the end
  bool queryDirectory(const FileId& id, bool restart,
                      std::vector<FileInfo>& entries) {
    entries.clear();
    startRequest(SMB2_QUERY_DIRECTORY);
    Writer w(req);
    w.u16(33);
    w.u8(FileDirectoryInformation);
    w.u8(restart ? SMB2_RESTART_SCANS : 0);
    w.u32(0);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    w.u16(SMB2_HEADER_SIZE + 32);
    w.u16(2);
    w.u32(maxTransact);
    w.utf16("*");
    std::vector<uint8_t>& resp = rx;
    if (transact(resp) != STATUS_SUCCESS) return false;
    Reader r(resp.data(), resp.size());
    size_t off = r.u16(66);
    size_t end = off + r.u32(68);
    while (off + 64 <= end && !r.hasError()) {
      FileInfo info;
      info.modified = fromFileTime(r.u64(off + 24));
      info.size = r.u64(off + 40);
      info.isDirectory = r.u32(off + 56) & FILE_ATTRIBUTE_DIRECTORY;
      info.name = r.utf16(off + 64, r.u32(off + 60));
      if (info.name != "." && info.name != "..") entries.push_back(info);
      uint32_t next = r.u32(off);
      if (next == 0) break;
      off += next;
    }
    return true;
  }

  uint32_t maxReadSize() const { return maxRead; }
  uint32_t maxWriteSize() const { return maxWrite; }

 protected:
  friend class SMBFile;
  static const uint32_t ACCESS_READ = 0x00120089;        // FILE_GENERIC_READ
  static const uint32_t ACCESS_READ_WRITE = 0x0012019F;  // + GENERIC_WRITE
  static const uint32_t ACCESS_ATTRIBUTES = 0x00100080;
  static const uint32_t ACCESS_LIST = 0x00100081;
  static const uint32_t ACCESS_DELETE = 0x00110080;
  static const uint32_t STATUS_PENDING = 0x00000103;

  Client& client;
  NtlmClient ntlm;
  std::string host, share;
  uint16_t port = SMB_DEFAULT_PORT;
  uint32_t maxIO = 0;
  uint32_t maxRead = 65536, maxWrite = 65536, maxTransact = 65536;
  uint32_t timeout = 10000;
  uint32_t pendingTimeout = 60000;
  uint32_t status = STATUS_SUCCESS;
  uint32_t generation = 1;
  uint64_t messageId = 0;
  uint64_t sessionId = 0;
  uint32_t treeId = 0;
  uint16_t command = 0;
  bool connectedFlag = false;
  bool signingRequired = false;
  bool signing = false;
  uint8_t signingKey[16] = {0};
  std::vector<uint8_t> req, rx;

  static uint64_t fromFileTime(uint64_t ft) {
    uint64_t s = ft / 10000000ULL;
    return s > FILETIME_UNIX_OFFSET ? s - FILETIME_UNIX_OFFSET : 0;
  }

  /// "/dir/file" with '/' separators
  static std::string normalize(const char* path) {
    std::string p = path ? path : "";
    for (auto& c : p)
      if (c == '\\') c = '/';
    if (p.empty() || p[0] != '/') p = "/" + p;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
  }

  /// "dir\file" as used by SMB
  static std::string toSmbName(const char* path) {
    std::string p = normalize(path).substr(1);
    for (auto& c : p)
      if (c == '/') c = '\\';
    return p;
  }

  bool ensureConnected() {
    if (isConnected()) return true;
    if (host.empty()) return false;
    SMB_LOGI("reconnecting to %s", host.c_str());
    return connect();
  }

  bool connect() {
    client.stop();
    connectedFlag = false;
    signing = false;
    sessionId = 0;
    treeId = 0;
    messageId = 0;
    generation++;
    if (!client.connect(host.c_str(), port)) {
      SMB_LOGE("could not connect to %s:%u", host.c_str(), port);
      return false;
    }
#if defined(IS_DESKTOP)
    // the emulator's available() waits for data up to the timeout
    client.setTimeout(0);
#endif
    connectedFlag = true;
    if (!negotiate() || !sessionSetup() || !treeConnect()) {
      SMB_LOGE("connect to \\\\%s\\%s failed: 0x%08x", host.c_str(),
               share.c_str(), (unsigned)status);
      client.stop();
      connectedFlag = false;
      return false;
    }
    SMB_LOGI("connected to \\\\%s\\%s", host.c_str(), share.c_str());
    return true;
  }

  bool negotiate() {
    startRequest(SMB2_NEGOTIATE);
    Writer w(req);
    w.u16(36);
    w.u16(2);
    w.u16(signingRequired ? SMB2_NEGOTIATE_SIGNING_REQUIRED
                          : SMB2_NEGOTIATE_SIGNING_ENABLED);
    w.u16(0);
    w.u32(0);
    for (int i = 0; i < 16; i++) w.u8(Platform::randomByte());
    w.u64(0);
    w.u16(SMB2_DIALECT_0202);
    w.u16(SMB2_DIALECT_0210);
    std::vector<uint8_t> resp;
    if (transact(resp) != STATUS_SUCCESS) return false;
    Reader r(resp.data(), resp.size());
    uint16_t serverMode = r.u16(66);
    if (serverMode & SMB2_NEGOTIATE_SIGNING_REQUIRED) signingRequired = true;
    uint32_t limit = maxIO;
    if (limit == 0) {
      limit = 65536;
#if defined(ESP32)
      if (!psramFound()) limit = 32768;
#endif
    }
    maxTransact = min32(r.u32(92), limit);
    maxRead = min32(r.u32(96), limit);
    maxWrite = min32(r.u32(100), limit);
    SMB_LOGD("dialect 0x%04x, max read %u", r.u16(68), (unsigned)maxRead);
    return !r.hasError() && maxRead > 0 && maxWrite > 0;
  }

  static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

  bool sessionSetup() {
    std::vector<uint8_t> token = ntlm.negotiateToken();
    std::vector<uint8_t> resp;
    for (int step = 0; step < 2; step++) {
      startRequest(SMB2_SESSION_SETUP);
      Writer w(req);
      w.u16(25);
      w.u8(0);
      w.u8(signingRequired ? SMB2_NEGOTIATE_SIGNING_REQUIRED
                           : SMB2_NEGOTIATE_SIGNING_ENABLED);
      w.u32(0);
      w.u32(0);
      w.u16(SMB2_HEADER_SIZE + 24);
      w.u16((uint16_t)token.size());
      w.u64(0);
      w.bytes(token.data(), token.size());
      uint32_t rc = transact(resp);
      Reader r(resp.data(), resp.size());
      if (step == 0) {
        if (rc != STATUS_MORE_PROCESSING_REQUIRED) return false;
        sessionId = r.u64(40);
        const uint8_t* blob = r.ptr(r.u16(68), r.u16(70));
        if (blob == nullptr ||
            !ntlm.authenticateToken(blob, r.u16(70), token)) {
          status = STATUS_LOGON_FAILURE;
          return false;
        }
      } else {
        if (rc != STATUS_SUCCESS) return false;
        bool guest = r.u16(66) & SMB2_SESSION_FLAG_IS_GUEST;
        if (ntlm.hasSessionKey() && !guest) {
          memcpy(signingKey, ntlm.sessionKey(), 16);
          signing = signingRequired;
        } else if (signingRequired) {
          SMB_LOGE("signing required, but not possible for guests");
          return false;
        }
      }
    }
    return true;
  }

  bool treeConnect() {
    std::string path = "\\\\" + host + "\\" + share;
    startRequest(SMB2_TREE_CONNECT);
    Writer w(req);
    w.u16(9);
    w.u16(0);
    w.u16(SMB2_HEADER_SIZE + 8);
    size_t lenPos = w.pos();
    w.u16(0);
    w.put16(lenPos, (uint16_t)w.utf16(path));
    std::vector<uint8_t> resp;
    if (transact(resp) != STATUS_SUCCESS) return false;
    treeId = Reader(resp.data(), resp.size()).u32(36);
    return true;
  }

  bool setInfo(const FileId& id, uint8_t infoClass,
               const std::vector<uint8_t>& data) {
    startRequest(SMB2_SET_INFO);
    Writer w(req);
    w.u16(33);
    w.u8(SMB2_0_INFO_FILE);
    w.u8(infoClass);
    w.u32((uint32_t)data.size());
    w.u16(SMB2_HEADER_SIZE + 32);
    w.u16(0);
    w.u32(0);
    w.u64(id.persistent);
    w.u64(id.volatileId);
    w.bytes(data.data(), data.size());
    std::vector<uint8_t> resp;
    return transact(resp) == STATUS_SUCCESS;
  }

  bool deletePath(const char* path, bool dir) {
    if (!ensureConnected()) return false;
    FileId id;
    FileInfo info;
    uint32_t options = dir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE;
    if (!create(path, ACCESS_DELETE, FILE_OPEN, options, id, info))
      return false;
    // unlike FILE_DELETE_ON_CLOSE this reports errors (e.g. not empty)
    std::vector<uint8_t> data = {1};
    bool ok = setInfo(id, FileDispositionInformation, data);
    uint32_t rc = status;
    closeFile(id);
    status = rc;
    return ok;
  }

  // ---------------------------------------------------------- messages

  void startRequest(uint16_t cmd) {
    command = cmd;
    req.assign(SMB2_HEADER_SIZE, 0);
  }

  void bodyU16(uint16_t structureSize) {
    Writer w(req);
    w.u16(structureSize);
    w.u16(0);
  }

  /// Sends the request and receives the response; returns the status
  uint32_t transact(std::vector<uint8_t>& resp) {
    uint64_t id = messageId++;
    Writer w(req);
    req[0] = 0xFE;
    req[1] = 'S';
    req[2] = 'M';
    req[3] = 'B';
    w.put16(4, 64);
    w.put16(6, 1);  // credit charge
    w.put16(12, command);
    w.put16(14, 32);  // credits requested
    w.put64(24, id);
    w.put32(36, treeId);
    w.put64(40, sessionId);
    if (signing) {
      w.put32(16, SMB2_FLAGS_SIGNED);
      uint8_t mac[32];
      Crypto::hmacSha256(signingKey, 16, req.data(), req.size(), nullptr, 0,
                         mac);
      memcpy(req.data() + 48, mac, 16);
    }
    status = STATUS_INTERNAL_ERROR;
    // a single write: separate writes are delayed by Nagle + delayed ACK
    size_t len = req.size();
    const uint8_t frame[4] = {0, (uint8_t)(len >> 16), (uint8_t)(len >> 8),
                              (uint8_t)len};
    req.insert(req.begin(), frame, frame + 4);
    if (!send(req.data(), req.size())) return fail();
    // wait for the final response (skip interim STATUS_PENDING responses)
    uint32_t wait = timeout;
    while (true) {
      if (!receive(resp, wait)) return fail();
      Reader r(resp.data(), resp.size());
      if (resp.size() < SMB2_HEADER_SIZE || r.u32(0) != 0x424D53FE)
        return fail();
      if (r.u64(24) != id) continue;
      status = r.u32(8);
      if (status == STATUS_PENDING) {
        // the server answers later: wait longer than usual
        wait = pendingTimeout;
        SMB_LOGD("command %u is pending", command);
        continue;
      }
      if (signing && (r.u32(16) & SMB2_FLAGS_SIGNED) &&
          !verify(resp.data(), resp.size())) {
        SMB_LOGE("invalid signature");
        status = STATUS_ACCESS_DENIED;
      }
      if (status != STATUS_SUCCESS && status != STATUS_END_OF_FILE &&
          status != STATUS_NO_MORE_FILES &&
          status != STATUS_MORE_PROCESSING_REQUIRED) {
        SMB_LOGD("command %u -> 0x%08x", command, (unsigned)status);
      }
      return status;
    }
  }

  uint32_t fail() {
    SMB_LOGE("connection lost");
    client.stop();
    connectedFlag = false;
    status = STATUS_INTERNAL_ERROR;
    return status;
  }

  bool verify(const uint8_t* msg, size_t len) {
    uint8_t header[SMB2_HEADER_SIZE];
    memcpy(header, msg, SMB2_HEADER_SIZE);
    memset(header + 48, 0, 16);
    uint8_t mac[32];
    Crypto::hmacSha256(signingKey, 16, header, SMB2_HEADER_SIZE,
                       msg + SMB2_HEADER_SIZE, len - SMB2_HEADER_SIZE, mac);
    return Crypto::equals(mac, msg + 48, 16);
  }

  bool send(const uint8_t* data, size_t len) {
    size_t sent = 0;
    uint32_t start = millis();
    while (sent < len) {
      size_t n = client.write(data + sent, len - sent);
      if (n == 0 || n > len - sent) {
        if (!client.connected() || millis() - start > timeout) return false;
        delay(1);
        continue;
      }
      sent += n;
    }
    return true;
  }

  bool readExact(uint8_t* data, size_t len, uint32_t timeout) {
    size_t got = 0;
    uint32_t start = millis();
    while (got < len) {
      int avail = client.available();
      if (avail > 0) {
        size_t n = len - got < (size_t)avail ? len - got : (size_t)avail;
        int rc = client.read(data + got, n);
        if (rc > 0) {
          got += rc;
          start = millis();
          continue;
        }
      }
      if (!client.connected() || millis() - start > timeout) return false;
      delay(1);
    }
    return true;
  }

  bool receive(std::vector<uint8_t>& resp, uint32_t timeout) {
    uint8_t frame[4];
    while (true) {
      if (!readExact(frame, 4, timeout)) return false;
      size_t len = ((size_t)frame[1] << 16) | ((size_t)frame[2] << 8) |
                   frame[3];
      if (frame[0] == 0x85 && len == 0) continue;  // keep alive
      if (frame[0] != 0 || len > (maxRead > maxTransact ? maxRead : maxTransact) + 4096)
        return false;
      resp.resize(len);
      return readExact(resp.data(), len, timeout);
    }
  }
};

// ------------------------------------------------------------ SMBFile impl

inline bool SMBFile::isOpen() const {
  return h && h->open && h->client != nullptr &&
         h->generation == h->client->generation;
}

inline SMBFile::Handle::~Handle() { close(); }

inline void SMBFile::Handle::close() {
  if (!open) return;
  if (client != nullptr && generation == client->generation) {
    flushWrite();
    client->closeFile(id);
  }
  open = false;
}

inline bool SMBFile::Handle::flushWrite() {
  size_t done = 0;
  while (done < wbuf.size()) {
    int64_t n = client->writeFile(id, wbufOffset + done, wbuf.data() + done,
                                  wbuf.size() - done);
    if (n <= 0) {
      wbuf.clear();
      return false;
    }
    done += n;
  }
  wbuf.clear();
  return true;
}

inline void SMBFile::close() {
  if (h) h->close();
}

inline void SMBFile::flush() {
  if (!isOpen()) return;
  h->flushWrite();
  h->client->flushFile(h->id);
}

inline size_t SMBFile::read(uint8_t* buffer, size_t len) {
  if (!isOpen() || h->isDir) return 0;
  if (!h->wbuf.empty()) h->flushWrite();
  size_t done = 0;
  while (done < len) {
    uint64_t pos = h->pos;
    // from the read buffer
    if (pos >= h->rbufOffset && pos < h->rbufOffset + h->rbuf.size()) {
      size_t off = pos - h->rbufOffset;
      size_t n = h->rbuf.size() - off;
      if (n > len - done) n = len - done;
      memcpy(buffer + done, h->rbuf.data() + off, n);
      done += n;
      h->pos += n;
      continue;
    }
    int64_t n;
    if (len - done >= h->bufferSize) {
      // big reads go directly to the user buffer
      n = h->client->readFile(h->id, pos, buffer + done, len - done);
      if (n > 0) {
        done += n;
        h->pos += n;
      }
    } else {
      h->rbuf.resize(h->bufferSize);
      n = h->client->readFile(h->id, pos, h->rbuf.data(), h->bufferSize);
      h->rbuf.resize(n > 0 ? n : 0);
      h->rbufOffset = pos;
    }
    if (n <= 0) break;
  }
  return done;
}

inline size_t SMBFile::write(const uint8_t* buffer, size_t len) {
  if (!isOpen() || !h->writable || h->isDir) return 0;
  h->rbuf.clear();
  if (h->append) h->pos = h->size;
  if (!h->wbuf.empty() && h->pos != h->wbufOffset + h->wbuf.size()) {
    if (!h->flushWrite()) return 0;
  }
  size_t done = 0;
  if (h->wbuf.empty() && len >= h->bufferSize) {
    // big writes are sent directly
    while (done < len) {
      int64_t n = h->client->writeFile(h->id, h->pos, buffer + done,
                                       len - done);
      if (n <= 0) break;
      done += n;
      h->pos += n;
    }
  } else {
    if (h->wbuf.empty()) h->wbufOffset = h->pos;
    h->wbuf.insert(h->wbuf.end(), buffer, buffer + len);
    h->pos += len;
    done = len;
    if (h->wbuf.size() >= h->bufferSize && !h->flushWrite()) return 0;
  }
  if (h->pos > h->size) h->size = h->pos;
  return done;
}

inline bool SMBFile::truncate(uint64_t size) {
  if (!isOpen() || !h->writable) return false;
  if (!h->wbuf.empty()) h->flushWrite();
  h->rbuf.clear();
  if (!h->client->setFileSize(h->id, size)) return false;
  h->size = size;
  if (h->pos > size) h->pos = size;
  return true;
}

inline bool SMBFile::getNextEntry(FileInfo& info) {
  if (!isOpen() || !h->isDir) return false;
  while (h->entryIdx >= h->entries.size()) {
    if (h->dirEnd) return false;
    bool restart = !h->dirStarted;
    h->dirStarted = true;
    h->entryIdx = 0;
    if (!h->client->queryDirectory(h->id, restart, h->entries)) {
      h->entries.clear();
      h->dirEnd = true;
      return false;
    }
  }
  info = h->entries[h->entryIdx++];
  return true;
}

inline SMBFile SMBFile::openNextFile(const char* mode) {
  FileInfo info;
  if (!getNextEntry(info)) return SMBFile();
  std::string child = h->path == "/" ? "/" + info.name
                                     : h->path + "/" + info.name;
  return h->client->open(child.c_str(), mode);
}

}  // namespace smb
