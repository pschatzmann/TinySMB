#pragma once
#include <Arduino.h>
#include <ctype.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "SMBAuth.h"
#include "SMBAlloc.h"
#include "SMBBuffer.h"
#include "SMBCrypto.h"
#include "SMBDefs.h"
#include "SMBFileSystem.h"
#include "SMBLogger.h"
#include "SMBPlatform.h"
#include "SMBRpc.h"

#if defined(ESP32)
#include <esp32-hal-psram.h>
#endif

namespace smb {

/// Exported directory
struct Share {
  std::string name;
  FileSystem* fs = nullptr;
  std::string root;  ///< directory in the file system which is exported
  bool readOnly = false;
  std::string comment;
};

/**
 * @brief SMB2 (dialect 2.0.2 and 2.1) file server which works with any
 * Arduino network API server class (WiFiServer, EthernetServer...).
 *
 * Example: `WiFiServer wifiServer(SMB_DEFAULT_PORT);
 * SMBServer<WiFiServer> smbServer(wifiServer);`
 *
 * Supported: NTLMv2 user authentication (with SMB2 signing), guest access,
 * multiple shares and clients, directory listing, read, write, create,
 * delete, rename, truncate and share enumeration via srvsvc.
 *
 * @tparam TServer Arduino server class (e.g. WiFiServer)
 */
template <class TServer>
class SMBServer {
 public:
  using TClient = decltype(std::declval<TServer&>().accept());

  explicit SMBServer(TServer& server) : server(server) {}
  ~SMBServer() { end(); }

  /// Adds a share: returns false if a share with the same name exists
  bool addShare(const char* name, FileSystem& fs, const char* root = "/",
                bool readOnly = false, const char* comment = "") {
    if (findShare(name) >= 0) return false;
    Share share;
    share.name = name;
    share.fs = &fs;
    share.root = normalizeRoot(root);
    share.readOnly = readOnly;
    share.comment = comment;
    shares.push_back(share);
    return true;
  }

  /// Adds a user which can log in with NTLMv2. A read-only user can't
  /// create, update, rename or delete any files, even on writable shares.
  void addUser(const char* name, const char* password, bool readOnly = false) {
    User user;
    user.name = name;
    user.password = password;
    user.readOnly = readOnly;
    users.push_back(user);
  }

  /// Adds a user which only has read access to all shares
  void addReadOnlyUser(const char* name, const char* password) {
    addUser(name, password, true);
  }

  /// Changes the password of a user: active sessions stay logged in. Returns
  /// false if the user does not exist.
  bool setPassword(const char* name, const char* password) {
    User* user = findUser(name);
    if (user == nullptr) return false;
    user->password = password;
    return true;
  }

  /// Removes a user and logs off all of its active sessions. Returns false if
  /// the user does not exist.
  bool removeUser(const char* userName) {
    std::string name = userName;
    size_t count = users.size();
    for (size_t i = 0; i < users.size();) {
      if (equalsIgnoreCase(users[i].name, name)) {
        users.erase(users.begin() + i);
      } else {
        i++;
      }
    }
    if (users.size() == count) return false;
    for (auto& c : connections) {
      for (size_t i = 0; i < c->sessions.size();) {
        Session& s = c->sessions[i];
        if (s.authenticated && !s.guest &&
            equalsIgnoreCase(s.auth.userName(), name)) {
          SMB_LOGI("user '%s' removed: session closed", name.c_str());
          removeSession(*c, s.id);
        } else {
          i++;
        }
      }
    }
    return true;
  }

  /// Allows anonymous / guest logins (default: true if no users defined)
  void setGuestAccess(bool allow) {
    guestAccess = allow;
    guestDefined = true;
  }

  /// Defines the NetBIOS name of the server and the workgroup
  void setServerName(const char* name, const char* workgroup = "WORKGROUP") {
    serverName = name;
    domainName = workgroup;
  }

  /// Requires that all messages are signed (only for non guest sessions)
  void setSigningRequired(bool required) { signingRequired = required; }

  /// Max number of concurrent client connections
  void setMaxClients(int count) { maxClients = count; }

  /// Max read/write/transact size (0 = automatic)
  void setMaxIOSize(uint32_t size) { maxIO = size; }

  /// Logs (with level Info) a summary of the time spent for the storage, the
  /// processing and the network when a file which was read or written is
  /// closed. If intervalMs > 0, an additional progress line is logged while
  /// a file is still open, at most every intervalMs milliseconds, which is
  /// useful to monitor long transfers (e.g. large files kept open for a
  /// while) instead of only seeing a summary once the file is closed.
  void setTimingLog(bool active, uint32_t intervalMs = 0) {
    timingLog = active;
    timingLogIntervalMs = intervalMs;
  }

  /// Starts the server
  bool begin() {
    if (!guestDefined) guestAccess = users.empty();
    if (maxIO == 0) {
      maxIO = 65536;
#if defined(ESP32)
      if (!psramFound()) maxIO = 32768;
#endif
    }
    for (int i = 0; i < 16; i++) serverGuid[i] = Platform::randomByte();
    startTime = toFileTime(Platform::unixTime());
    server.begin();
    SMB_LOGI("server started (max io: %u)", (unsigned)maxIO);
    return true;
  }

  /// Stops the server and closes all connections
  void end() {
    for (auto& c : connections) {
      closeAll(*c);
      c->client.stop();
    }
    connections.clear();
  }

  /// Process the network requests: call in the Arduino loop()
  void loop() {
    if (hasPendingClient(server, 0)) {
      TClient client = server.accept();
      if (client) {
        if ((int)connections.size() < maxClients) {
          SMB_LOGI("new connection");
          connections.emplace_back(new Connection(client));
        } else {
          SMB_LOGI("too many connections");
          client.stop();
        }
      }
    }
    for (size_t i = 0; i < connections.size();) {
      Connection& c = *connections[i];
      if (!processConnection(c)) {
        SMB_LOGI("connection closed");
        closeAll(c);
        c.client.stop();
        connections.erase(connections.begin() + i);
      } else {
        i++;
      }
    }
  }

  /// Alias for loop()
  void doLoop() { loop(); }

  /// Provides the number of connected clients
  size_t clientCount() { return connections.size(); }

 protected:
  struct Session {
    uint64_t id = 0;
    bool authenticated = false;
    bool guest = false;
    bool readOnly = false;  ///< the user only has read access
    bool signingRequired = false;
    bool hasKey = false;
    uint8_t key[16] = {0};
    NtlmAuth auth;
  };

  struct Tree {
    uint32_t id = 0;
    uint64_t sessionId = 0;
    int share = -1;  // -1 = IPC$
    bool readOnly = false;  ///< share or user is read-only
  };

  /// Statistics of the reads and writes of an open file (setTimingLog())
  struct Timing {
    uint32_t reads = 0, writes = 0;
    uint64_t bytesRead = 0, bytesWritten = 0;
    uint64_t storageUs = 0, cpuUs = 0, sendUs = 0, receiveUs = 0;
    uint32_t startMs = 0;
    uint32_t lastLogMs = 0;  ///< millis() of the last progress log line
  };

  struct Open {
    uint64_t id = 0;
    uint32_t treeId = 0;
    uint64_t sessionId = 0;
    std::string path;  // relative to the share root
    bool isDir = false;
    bool isPipe = false;
    bool deleteOnClose = false;
    uint32_t dirIndex = 0;
    std::string pattern;
    PsramVector<FileInfo> dirEntries;  ///< cached listing (avoids
                                       ///< re-scanning storage on every
                                       ///< page); backing storage prefers
                                       ///< PSRAM since it can grow large
    bool dirListed = false;
    std::vector<uint8_t> pipeOut;
    Timing timing;
  };

  struct Connection {
    Connection(TClient& c) : client(c) {
#if defined(IS_DESKTOP)
      // the emulator's available() waits for data up to the timeout
      client.setTimeout(0);
#endif
    }
    TClient client;
    PsramVector<uint8_t> rx;  ///< receive buffer: up to maxIO + 4096 bytes,
                             ///< prefer PSRAM
    uint16_t dialect = 0;
    std::vector<Session> sessions;
    std::vector<Tree> trees;
    std::vector<Open> opens;
    // timing of the current request (setTimingLog())
    uint32_t rxStartUs = 0;   ///< first byte of the request received
    uint32_t receiveUs = 0;   ///< time to receive the request
    uint32_t storageUs = 0;   ///< storage time of the request
    uint64_t timedOpen = 0;   ///< id of the open which was read or written
  };

  /// Signing state of a response
  struct SignInfo {
    SignInfo() = default;
    SignInfo(const Session& s) : hasKey(s.hasKey), required(s.signingRequired) {
      memcpy(key, s.key, 16);
    }
    bool hasKey = false;
    bool required = false;
    bool active = false;
    uint8_t key[16] = {0};
  };

  /// Request context, also used to propagate state in compound requests
  struct Request {
    Reader r;
    uint16_t command = 0;
    uint32_t flags = 0;
    uint64_t sessionId = 0;
    uint32_t treeId = 0;
    Session* session = nullptr;
    Tree* tree = nullptr;
    uint64_t relatedFileId = ~0ULL;
    uint32_t relatedStatus = STATUS_SUCCESS;
    bool related() const { return flags & SMB2_FLAGS_RELATED_OPERATIONS; }
  };

  TServer& server;
  std::vector<std::unique_ptr<Connection>> connections;
  std::vector<Share> shares;
  std::vector<User> users;
  PsramVector<uint8_t> tx;    // shared response buffer: prefer PSRAM
  PsramVector<uint8_t> resp;  // prefer PSRAM (up to maxIO bytes)
  std::string serverName = "ARDUINO";
  std::string domainName = "WORKGROUP";
  uint8_t serverGuid[16] = {0};
  uint64_t startTime = 0;
  uint64_t nextSessionId = 0x100000001ULL;
  uint32_t nextTreeId = 1;
  uint64_t nextFileId = 1;
  uint32_t maxIO = 0;
  int maxClients = 4;
  bool guestAccess = true;
  bool guestDefined = false;
  bool signingRequired = false;
  bool keepErrorBody = false;
  bool timingLog = false;
  uint32_t timingLogIntervalMs = 0;  ///< setTimingLog() progress interval

  // ------------------------------------------------------------- transport

  /// Uses hasClient() if available to avoid blocking accept() calls
  template <class T>
  static auto hasPendingClient(T& srv, int) -> decltype(srv.hasClient()) {
    return srv.hasClient();
  }
  template <class T>
  static bool hasPendingClient(T&, long) {
    return true;
  }

  bool processConnection(Connection& c) {
    if (!c.client.connected()) return false;
    int avail = c.client.available();
    while (avail > 0) {
      size_t old = c.rx.size();
      if (old == 0) c.rxStartUs = micros();
      size_t n = avail > 4096 ? 4096 : (size_t)avail;
      c.rx.resize(old + n);
#if defined(IS_DESKTOP)
      int rc = c.client.readBytes(c.rx.data() + old, n);
#else
      int rc = c.client.read(c.rx.data() + old, n);
#endif
      c.rx.resize(old + (rc > 0 ? rc : 0));
      if (rc <= 0) break;
      // process all complete frames
      while (c.rx.size() >= 4) {
        size_t len = ((size_t)c.rx[1] << 16) | ((size_t)c.rx[2] << 8) |
                     (size_t)c.rx[3];
        if (len > maxIO + 4096) {
          SMB_LOGE("message too big: %u", (unsigned)len);
          return false;
        }
        if (c.rx.size() < len + 4) break;
        c.receiveUs = micros() - c.rxStartUs;
        if (!processFrame(c, c.rx[0], c.rx.data() + 4, len)) return false;
        c.rx.erase(c.rx.begin(), c.rx.begin() + len + 4);
        c.rxStartUs = micros();  // the next request is already arriving
      }
      avail = c.client.available();
    }
    if (c.rx.empty() && c.rx.capacity() > 8192) {
      PsramVector<uint8_t>().swap(c.rx);
    }
    return true;
  }

  bool processFrame(Connection& c, uint8_t type, const uint8_t* data,
                    size_t len) {
    if (type == 0x85) return true;  // keep alive
    if (type == 0x81) {
      // NetBIOS session request (port 139): positive response
      const uint8_t ok[4] = {0x82, 0, 0, 0};
      return send(c, ok, 4);
    }
    if (type != 0 || len < 4) return false;
    if (data[0] == 0xFF && memcmp(data + 1, "SMB", 3) == 0) {
      return processSmb1(c, data, len);
    }
    if (data[0] == 0xFE && memcmp(data + 1, "SMB", 3) == 0) {
      return processSmb2(c, data, len);
    }
    SMB_LOGE("unsupported protocol 0x%02x", data[0]);
    return false;
  }

  bool send(Connection& c, const uint8_t* data, size_t len) {
    size_t sent = 0;
    uint32_t start = millis();
    while (sent < len) {
      size_t n = c.client.write(data + sent, len - sent);
      // some implementations return (size_t)-1 if the socket would block
      if (n == 0 || n > len - sent) {
        if (!c.client.connected() || millis() - start > 10000) return false;
        delay(1);
        continue;
      }
      sent += n;
    }
    return true;
  }

  /// SMB1 negotiate: we only support the upgrade to SMB2
  bool processSmb1(Connection& c, const uint8_t* data, size_t len) {
    if (len < 35 || data[4] != 0x72) return false;
    std::string dialects((const char*)data + 35, len - 35);
    uint16_t dialect = 0;
    if (dialects.find("SMB 2.???") != std::string::npos) {
      dialect = SMB2_DIALECT_WILDCARD;
    } else if (dialects.find("SMB 2.002") != std::string::npos) {
      dialect = SMB2_DIALECT_0202;
    }
    if (dialect == 0) {
      SMB_LOGI("client does not support SMB2");
      return false;
    }
    if (dialect != SMB2_DIALECT_WILDCARD) c.dialect = dialect;
    resp.assign(SMB2_HEADER_SIZE, 0);
    Writer w(resp);
    writeNegotiateBody(w, dialect);
    writeHeader(resp, 0, STATUS_SUCCESS, SMB2_NEGOTIATE, 1, 0, 0, 0, 0);
    return sendResponses(c, {resp});
  }

  bool sendResponses(Connection& c, const std::vector<PsramVector<uint8_t>>& r) {
    tx.assign(4, 0);
    for (auto& part : r) tx.insert(tx.end(), part.begin(), part.end());
    size_t len = tx.size() - 4;
    tx[1] = (uint8_t)(len >> 16);
    tx[2] = (uint8_t)(len >> 8);
    tx[3] = (uint8_t)len;
    bool ok = send(c, tx.data(), tx.size());
    if (tx.capacity() > 16384) PsramVector<uint8_t>().swap(tx);
    return ok;
  }

  // -------------------------------------------------------------- dispatch

  bool processSmb2(Connection& c, const uint8_t* data, size_t len) {
    uint32_t startUs = micros();
    c.timedOpen = 0;
    c.storageUs = 0;
    std::vector<PsramVector<uint8_t>> responses;
    std::vector<SignInfo> signInfos;
    Request req;
    size_t off = 0;
    while (off + SMB2_HEADER_SIZE <= len) {
      uint32_t next = Reader(data + off, len - off).u32(20);
      size_t msgLen = next ? next : len - off;
      if (msgLen < SMB2_HEADER_SIZE || off + msgLen > len) return false;
      const uint8_t* msg = data + off;
      req.r = Reader(msg, msgLen);
      req.command = req.r.u16(12);
      req.flags = req.r.u32(16);
      uint64_t messageId = req.r.u64(24);
      uint16_t credits = req.r.u16(14);
      uint16_t charge = req.r.u16(6);
      if (!req.related()) {
        req.sessionId = req.r.u64(40);
        req.treeId = req.r.u32(36);
        req.relatedFileId = ~0ULL;
        req.relatedStatus = STATUS_SUCCESS;
      } else {
        if (req.r.u64(40) != ~0ULL) req.sessionId = req.r.u64(40);
        if (req.r.u32(36) != ~0U) req.treeId = req.r.u32(36);
      }
      if (req.command == SMB2_CANCEL) {
        off += msgLen;
        if (!next) break;
        continue;  // no response
      }

      // capture the signing key: the session might be removed by LOGOFF
      SignInfo sign;
      Session* before = findSession(c, req.sessionId);
      if (before != nullptr) sign = SignInfo(*before);

      resp.assign(SMB2_HEADER_SIZE, 0);
      keepErrorBody = false;
      uint32_t status = dispatch(c, req, msg, msgLen);
      if (status != STATUS_SUCCESS && status != STATUS_BUFFER_OVERFLOW &&
          status != STATUS_MORE_PROCESSING_REQUIRED && !keepErrorBody) {
        writeError();
      }
      if (req.command == SMB2_CREATE && status != STATUS_SUCCESS) {
        req.relatedStatus = status;
      }
      SMB_LOGD("cmd %u -> 0x%08x", req.command, (unsigned)status);
      Session* s = findSession(c, req.sessionId);
      if (s != nullptr) sign = SignInfo(*s);
      writeHeader(resp, req.r.u32(32), status, req.command,
                  credits ? credits : 1, charge, messageId, req.treeId,
                  req.sessionId, req.flags & SMB2_FLAGS_RELATED_OPERATIONS);
      sign.active = sign.hasKey && (sign.required ||
                                    (req.flags & SMB2_FLAGS_SIGNED));
      if (req.command == SMB2_SESSION_SETUP && status != STATUS_SUCCESS)
        sign.active = false;
      responses.push_back(resp);
      signInfos.push_back(sign);
      off += msgLen;
      if (!next) break;
    }
    if (responses.empty()) return true;
    // link compound responses and sign them
    for (size_t i = 0; i < responses.size(); i++) {
      auto& r = responses[i];
      bool last = i + 1 == responses.size();
      if (!last) {
        while (r.size() % 8) r.push_back(0);
        Writer(r).put32(20, (uint32_t)r.size());
      }
      if (signInfos[i].active) signMessage(r.data(), r.size(), signInfos[i].key);
    }
    if (c.timedOpen == 0) return sendResponses(c, responses);
    uint32_t sendStartUs = micros();
    bool ok = sendResponses(c, responses);
    Open* o = findOpenById(c, c.timedOpen);
    if (o != nullptr) {
      Timing& t = o->timing;
      t.storageUs += c.storageUs;
      t.cpuUs += sendStartUs - startUs - c.storageUs;
      t.sendUs += micros() - sendStartUs;
      t.receiveUs += c.receiveUs;
    }
    return ok;
  }

  Open* findOpenById(Connection& c, uint64_t id) {
    for (auto& o : c.opens)
      if (o.id == id) return &o;
    return nullptr;
  }

  /// Records the storage time of a read or write
  void timeStorage(Connection& c, Open& o, uint32_t startUs, bool isRead,
                   int64_t bytes) {
    c.storageUs += micros() - startUs;
    c.timedOpen = o.id;
    Timing& t = o.timing;
    // start with the reception of the first request
    if (t.reads == 0 && t.writes == 0) {
      t.startMs = millis() - (micros() - c.rxStartUs) / 1000;
      t.lastLogMs = t.startMs;
    }
    if (isRead) {
      t.reads++;
      if (bytes > 0) t.bytesRead += bytes;
    } else {
      t.writes++;
      if (bytes > 0) t.bytesWritten += bytes;
    }
    // optional progress log while a long transfer is still in progress
    // (the final summary is logged separately when the file is closed)
    if (timingLog && timingLogIntervalMs > 0) {
      uint32_t now = millis();
      if (now - t.lastLogMs >= timingLogIntervalMs) {
        t.lastLogMs = now;
        logTiming(o, "progress", 0);
      }
    }
  }

  void logTiming(const Open& o, const char* label, uint32_t closeUs) {
    const Timing& t = o.timing;
    uint64_t bytes = t.bytesRead + t.bytesWritten;
    uint32_t ms = millis() - t.startMs;
    uint64_t storage = t.storageUs + closeUs;
    uint64_t wallUs = (uint64_t)ms * 1000;
    uint64_t known = storage + t.cpuUs + t.sendUs + t.receiveUs;
    uint64_t wait = wallUs > known ? wallUs - known : 0;
    auto pct = [&](uint64_t us) {
      return wallUs ? (unsigned)(us * 100 / wallUs) : 0u;
    };
    SMB_LOGI("timing %s %s: %u reads, %u writes, %llu bytes in %u ms (%u "
             "KB/s)",
             label, o.path.c_str(), (unsigned)t.reads, (unsigned)t.writes,
             (unsigned long long)bytes, (unsigned)ms,
             ms ? (unsigned)(bytes / ms) : 0u);
    SMB_LOGI(
        "timing   storage %u ms (%u%%), cpu %u ms (%u%%), send %u ms (%u%%), "
        "receive %u ms (%u%%), wait %u ms (%u%%)",
        (unsigned)(storage / 1000), pct(storage), (unsigned)(t.cpuUs / 1000),
        pct(t.cpuUs), (unsigned)(t.sendUs / 1000), pct(t.sendUs),
        (unsigned)(t.receiveUs / 1000), pct(t.receiveUs),
        (unsigned)(wait / 1000), pct(wait));
  }

  uint32_t dispatch(Connection& c, Request& req, const uint8_t* msg,
                    size_t len) {
    req.session = nullptr;
    req.tree = nullptr;
    switch (req.command) {
      case SMB2_NEGOTIATE:
        return onNegotiate(c, req);
      case SMB2_SESSION_SETUP:
        return onSessionSetup(c, req);
      case SMB2_ECHO:
        return body16(4);
      default:
        break;
    }
    // all other commands require a valid session
    req.session = findSession(c, req.sessionId);
    if (req.session == nullptr || !req.session->authenticated)
      return STATUS_USER_SESSION_DELETED;
    if ((req.flags & SMB2_FLAGS_SIGNED) && req.session->hasKey &&
        !verifySignature(msg, len, req.session->key)) {
      SMB_LOGI("invalid signature");
      return STATUS_ACCESS_DENIED;
    }
    if (req.command == SMB2_LOGOFF) return onLogoff(c, req);
    if (req.command == SMB2_TREE_CONNECT) return onTreeConnect(c, req);

    // all other commands require a valid tree
    req.tree = findTree(c, req.treeId, req.sessionId);
    if (req.tree == nullptr) return STATUS_NETWORK_NAME_DELETED;
    switch (req.command) {
      case SMB2_TREE_DISCONNECT:
        return onTreeDisconnect(c, req);
      case SMB2_CREATE:
        return onCreate(c, req);
      case SMB2_CLOSE:
        return onClose(c, req);
      case SMB2_FLUSH:
        return onFlush(c, req);
      case SMB2_READ:
        return onRead(c, req);
      case SMB2_WRITE:
        return onWrite(c, req);
      case SMB2_LOCK:
        return findOpen(c, req, 72) ? body16(4) : STATUS_FILE_CLOSED;
      case SMB2_IOCTL:
        return onIoctl(c, req);
      case SMB2_QUERY_DIRECTORY:
        return onQueryDirectory(c, req);
      case SMB2_QUERY_INFO:
        return onQueryInfo(c, req);
      case SMB2_SET_INFO:
        return onSetInfo(c, req);
      case SMB2_CHANGE_NOTIFY:
      default:
        return STATUS_NOT_SUPPORTED;
    }
  }

  // ------------------------------------------------------- header + signing

  void writeHeader(PsramVector<uint8_t>& r, uint32_t pid, uint32_t status,
                   uint16_t command, uint16_t credits, uint16_t charge,
                   uint64_t messageId, uint32_t treeId, uint64_t sessionId,
                   uint32_t extraFlags = 0) {
    Writer w(r);
    r[0] = 0xFE;
    r[1] = 'S';
    r[2] = 'M';
    r[3] = 'B';
    w.put16(4, 64);
    w.put16(6, charge);
    w.put32(8, status);
    w.put16(12, command);
    w.put16(14, credits > 256 ? 256 : credits);
    w.put32(16, SMB2_FLAGS_SERVER_TO_REDIR | extraFlags);
    w.put32(20, 0);
    w.put64(24, messageId);
    w.put32(32, pid);
    w.put32(36, treeId);
    w.put64(40, sessionId);
    memset(r.data() + 48, 0, 16);
  }

  void writeError() {
    resp.resize(SMB2_HEADER_SIZE);
    Writer w(resp);
    w.u16(9);
    w.u16(0);
    w.u32(0);
    w.u8(0);
  }

  /// Writes a response body which only consists of the structure size
  uint32_t body16(uint16_t structureSize) {
    Writer w(resp);
    w.u16(structureSize);
    w.u16(0);
    return STATUS_SUCCESS;
  }

  static void signMessage(uint8_t* msg, size_t len, const uint8_t key[16]) {
    uint32_t flags = Reader(msg, len).u32(16) | SMB2_FLAGS_SIGNED;
    msg[16] = (uint8_t)flags;
    msg[17] = (uint8_t)(flags >> 8);
    msg[18] = (uint8_t)(flags >> 16);
    msg[19] = (uint8_t)(flags >> 24);
    memset(msg + 48, 0, 16);
    uint8_t mac[32];
    Crypto::hmacSha256(key, 16, msg, len, nullptr, 0, mac);
    memcpy(msg + 48, mac, 16);
  }

  static bool verifySignature(const uint8_t* msg, size_t len,
                              const uint8_t key[16]) {
    uint8_t mac[32];
    // the signature is calculated with a zeroed signature field
    uint8_t header[SMB2_HEADER_SIZE];
    memcpy(header, msg, SMB2_HEADER_SIZE);
    memset(header + 48, 0, 16);
    Crypto::hmacSha256(key, 16, header, SMB2_HEADER_SIZE,
                       msg + SMB2_HEADER_SIZE, len - SMB2_HEADER_SIZE, mac);
    return Crypto::equals(mac, msg + 48, 16);
  }

  // ------------------------------------------------------------ negotiate

  template <class Vec>
  void writeNegotiateBody(Writer<Vec>& w, uint16_t dialect) {
    std::vector<uint8_t> token = NtlmAuth::negotiateToken();
    w.u16(65);
    w.u16(SMB2_NEGOTIATE_SIGNING_ENABLED |
          (signingRequired ? SMB2_NEGOTIATE_SIGNING_REQUIRED : 0));
    w.u16(dialect);
    w.u16(0);
    w.bytes(serverGuid, 16);
    w.u32(0);  // capabilities
    w.u32(maxIO);
    w.u32(maxIO);
    w.u32(maxIO);
    w.u64(toFileTime(Platform::unixTime()));
    w.u64(startTime);
    w.u16(SMB2_HEADER_SIZE + 64);
    w.u16((uint16_t)token.size());
    w.u32(0);
    w.bytes(token.data(), token.size());
  }

  uint32_t onNegotiate(Connection& c, Request& req) {
    uint16_t count = req.r.u16(66);
    uint16_t best = 0;
    for (int i = 0; i < count; i++) {
      uint16_t d = req.r.u16(100 + 2 * i);
      if ((d == SMB2_DIALECT_0202 || d == SMB2_DIALECT_0210) && d > best)
        best = d;
    }
    if (best == 0) {
      SMB_LOGI("no supported dialect");
      return STATUS_NOT_SUPPORTED;
    }
    c.dialect = best;
    Writer w(resp);
    writeNegotiateBody(w, best);
    return STATUS_SUCCESS;
  }

  // -------------------------------------------------------------- session

  /// Provides the user which is used for the login (the last with the name)
  User* findUser(const char* name) {
    User* result = nullptr;
    for (auto& u : users)
      if (equalsIgnoreCase(u.name, name)) result = &u;
    return result;
  }

  Session* findSession(Connection& c, uint64_t id) {
    for (auto& s : c.sessions)
      if (s.id == id) return &s;
    return nullptr;
  }

  uint32_t onSessionSetup(Connection& c, Request& req) {
    Session* s = nullptr;
    if (req.sessionId == 0) {
      c.sessions.emplace_back();
      s = &c.sessions.back();
      s->id = nextSessionId++;
      s->auth.setNames(serverName, domainName);
      req.sessionId = s->id;
    } else {
      s = findSession(c, req.sessionId);
      if (s == nullptr) return STATUS_USER_SESSION_DELETED;
    }
    uint8_t clientMode = req.r.u8(67);
    uint16_t blobOff = req.r.u16(76);
    uint16_t blobLen = req.r.u16(78);
    const uint8_t* blob = req.r.ptr(blobOff, blobLen);
    if (blob == nullptr) return STATUS_INVALID_PARAMETER;

    std::vector<uint8_t> out;
    AuthResult result = s->auth.process(blob, blobLen, users, guestAccess, out);
    uint16_t sessionFlags = 0;
    uint32_t status = STATUS_SUCCESS;
    switch (result) {
      case AuthResult::Continue:
        status = STATUS_MORE_PROCESSING_REQUIRED;
        break;
      case AuthResult::Success:
        s->authenticated = true;
        s->readOnly = s->auth.isReadOnly();
        s->hasKey = true;
        memcpy(s->key, s->auth.sessionKey(), 16);
        s->signingRequired =
            signingRequired || (clientMode & SMB2_NEGOTIATE_SIGNING_REQUIRED);
        SMB_LOGI("user '%s' logged in%s", s->auth.userName().c_str(),
                 s->readOnly ? " (read-only)" : "");
        break;
      case AuthResult::Guest:
        s->authenticated = true;
        s->guest = true;
        sessionFlags = SMB2_SESSION_FLAG_IS_GUEST;
        SMB_LOGI("guest logged in");
        break;
      default:
        SMB_LOGI("login failed for '%s'", s->auth.userName().c_str());
        removeSession(c, s->id);
        return STATUS_LOGON_FAILURE;
    }
    Writer w(resp);
    w.u16(9);
    w.u16(sessionFlags);
    w.u16(out.empty() ? 0 : SMB2_HEADER_SIZE + 8);
    w.u16((uint16_t)out.size());
    w.bytes(out.data(), out.size());
    return status;
  }

  void removeSession(Connection& c, uint64_t id) {
    for (size_t i = 0; i < c.opens.size();) {
      if (c.opens[i].sessionId == id) {
        closeOpen(c, i);
      } else {
        i++;
      }
    }
    for (size_t i = 0; i < c.trees.size();) {
      if (c.trees[i].sessionId == id) {
        c.trees.erase(c.trees.begin() + i);
      } else {
        i++;
      }
    }
    for (size_t i = 0; i < c.sessions.size(); i++) {
      if (c.sessions[i].id == id) {
        c.sessions.erase(c.sessions.begin() + i);
        break;
      }
    }
  }

  uint32_t onLogoff(Connection& c, Request& req) {
    removeSession(c, req.sessionId);
    return body16(4);
  }

  // ----------------------------------------------------------------- tree

  int findShare(const std::string& name) {
    for (size_t i = 0; i < shares.size(); i++)
      if (equalsIgnoreCase(shares[i].name, name)) return (int)i;
    return -1;
  }

  Tree* findTree(Connection& c, uint32_t id, uint64_t sessionId) {
    for (auto& t : c.trees)
      if (t.id == id && t.sessionId == sessionId) return &t;
    return nullptr;
  }

  uint32_t onTreeConnect(Connection& c, Request& req) {
    std::string path = req.r.utf16(req.r.u16(68), req.r.u16(70));
    size_t pos = path.find_last_of('\\');
    std::string name = pos == std::string::npos ? path : path.substr(pos + 1);
    Tree tree;
    tree.sessionId = req.sessionId;
    bool ipc = equalsIgnoreCase(name, "IPC$");
    if (!ipc) {
      tree.share = findShare(name);
      if (tree.share < 0) {
        SMB_LOGI("unknown share '%s'", name.c_str());
        return STATUS_BAD_NETWORK_NAME;
      }
    }
    tree.readOnly = !ipc && (shares[tree.share].readOnly ||
                             (req.session != nullptr && req.session->readOnly));
    tree.id = nextTreeId++;
    c.trees.push_back(tree);
    req.treeId = tree.id;
    bool readOnly = tree.readOnly;
    Writer w(resp);
    w.u16(16);
    w.u8(ipc ? SMB2_SHARE_TYPE_PIPE : SMB2_SHARE_TYPE_DISK);
    w.u8(0);
    w.u32(0x00000030);  // SMB2_SHAREFLAG_NO_CACHING
    w.u32(0);
    w.u32(readOnly ? 0x001200A9 : 0x001F01FF);
    SMB_LOGI("connected to '%s'", name.c_str());
    return STATUS_SUCCESS;
  }

  uint32_t onTreeDisconnect(Connection& c, Request& req) {
    for (size_t i = 0; i < c.opens.size();) {
      if (c.opens[i].treeId == req.tree->id &&
          c.opens[i].sessionId == req.sessionId) {
        closeOpen(c, i);
      } else {
        i++;
      }
    }
    for (size_t i = 0; i < c.trees.size(); i++) {
      if (&c.trees[i] == req.tree) {
        c.trees.erase(c.trees.begin() + i);
        break;
      }
    }
    req.tree = nullptr;
    return body16(4);
  }

  // ---------------------------------------------------------------- files

  Share* shareOf(Request& req) {
    if (req.tree == nullptr || req.tree->share < 0) return nullptr;
    return &shares[req.tree->share];
  }

  /// Path in the file system for a share relative path
  static std::string fsPath(const Share& share, const std::string& rel) {
    if (share.root == "/") return rel;
    return rel == "/" ? share.root : share.root + rel;
  }

  static std::string normalizeRoot(const char* root) {
    std::string r = root == nullptr ? "/" : root;
    if (r.empty() || r[0] != '/') r = "/" + r;
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    return r;
  }

  /// Converts a SMB name into a share relative path; returns false if invalid
  static bool toPath(std::string name, std::string& path) {
    for (auto& ch : name)
      if (ch == '\\') ch = '/';
    // strip default data stream suffix
    const char* streams[] = {"::$DATA", ":$DATA"};
    for (auto s : streams) {
      size_t n = strlen(s);
      if (name.size() >= n && name.compare(name.size() - n, n, s) == 0)
        name.resize(name.size() - n);
    }
    if (name.find_first_of(":*?<>\"|") != std::string::npos) return false;
    path = "/";
    size_t start = 0;
    while (start <= name.size()) {
      size_t end = name.find('/', start);
      if (end == std::string::npos) end = name.size();
      std::string part = name.substr(start, end - start);
      if (part == "..") return false;
      if (!part.empty() && part != ".") {
        if (path.size() > 1) path += "/";
        path += part;
      }
      start = end + 1;
    }
    return true;
  }

  Open* findOpen(Connection& c, Request& req, size_t offset) {
    uint64_t id = req.r.u64(offset + 8);
    if (req.related() && id == ~0ULL) id = req.relatedFileId;
    for (auto& o : c.opens) {
      if (o.id == id && o.sessionId == req.sessionId &&
          req.tree != nullptr && o.treeId == req.tree->id)
        return &o;
    }
    return nullptr;
  }

  uint32_t fileStatus(Request& req) {
    if (req.related() && req.relatedStatus != STATUS_SUCCESS)
      return req.relatedStatus;
    return STATUS_FILE_CLOSED;
  }

  static uint32_t attributes(const FileInfo& info) {
    uint32_t attr = info.isDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                     : FILE_ATTRIBUTE_ARCHIVE;
    if (info.name.size() > 1 && info.name[0] == '.' && info.name != "..")
      attr |= FILE_ATTRIBUTE_HIDDEN;
    return attr;
  }

  static uint64_t allocationSize(uint64_t size) {
    return (size + 4095) & ~4095ULL;
  }

  static uint64_t fileIndex(const std::string& path) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char ch : path) {
      h ^= (uint8_t)tolower((unsigned char)ch);
      h *= 0x100000001b3ULL;
    }
    return h;
  }

  /// Provides the file information for an open
  bool infoOf(Request& req, Open& o, FileInfo& info) {
    if (o.isPipe) {
      info = FileInfo();
      info.name = o.path;
      return true;
    }
    Share* share = shareOf(req);
    if (share == nullptr) return false;
    if (!share->fs->stat(fsPath(*share, o.path).c_str(), info)) return false;
    if (o.path == "/") info.isDirectory = true;
    return true;
  }

  uint32_t onCreate(Connection& c, Request& req) {
    uint32_t disposition = req.r.u32(100);
    uint32_t options = req.r.u32(104);
    std::string name = req.r.utf16(req.r.u16(108), req.r.u16(110));
    if (req.r.hasError()) return STATUS_INVALID_PARAMETER;
    if (c.opens.size() >= 256) return STATUS_INSUFFICIENT_RESOURCES;

    Open open;
    open.treeId = req.tree->id;
    open.sessionId = req.sessionId;
    FileInfo info;
    uint32_t action = FILE_OPENED;

    Share* share = shareOf(req);
    if (share == nullptr) {
      // IPC$: named pipes
      std::string pipe = name;
      for (auto& ch : pipe) ch = (char)tolower((unsigned char)ch);
      if (pipe.rfind("\\pipe\\", 0) == 0) pipe = pipe.substr(6);
      if (pipe != "srvsvc") return STATUS_OBJECT_NAME_NOT_FOUND;
      open.isPipe = true;
      open.path = pipe;
      info.name = pipe;
    } else {
      if (!toPath(name, open.path)) return STATUS_OBJECT_NAME_INVALID;
      FileSystem* fs = share->fs;
      std::string path = fsPath(*share, open.path);
      bool exists = fs->stat(path.c_str(), info);
      if (open.path == "/") {
        exists = true;
        info.isDirectory = true;
      }
      bool isDirRequest = options & FILE_DIRECTORY_FILE;
      if (!exists) {
        if (disposition == FILE_OPEN || disposition == FILE_OVERWRITE) {
          return parentExists(*share, open.path) ? STATUS_OBJECT_NAME_NOT_FOUND
                                                 : STATUS_OBJECT_PATH_NOT_FOUND;
        }
        if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
        if (!parentExists(*share, open.path))
          return STATUS_OBJECT_PATH_NOT_FOUND;
        bool ok = isDirRequest ? fs->mkdir(path.c_str())
                               : fs->createFile(path.c_str());
        if (!ok || !fs->stat(path.c_str(), info)) return STATUS_ACCESS_DENIED;
        action = FILE_CREATED;
      } else {
        if (disposition == FILE_CREATE) return STATUS_OBJECT_NAME_COLLISION;
        if (isDirRequest && !info.isDirectory) return STATUS_NOT_A_DIRECTORY;
        if ((options & FILE_NON_DIRECTORY_FILE) && info.isDirectory)
          return STATUS_FILE_IS_A_DIRECTORY;
        if (disposition == FILE_SUPERSEDE || disposition == FILE_OVERWRITE ||
            disposition == FILE_OVERWRITE_IF) {
          if (info.isDirectory) return STATUS_INVALID_PARAMETER;
          if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
          if (!fs->createFile(path.c_str())) return STATUS_ACCESS_DENIED;
          info.size = 0;
          action = disposition == FILE_SUPERSEDE ? FILE_SUPERSEDED
                                                 : FILE_OVERWRITTEN;
        }
      }
      open.isDir = info.isDirectory;
      if (options & FILE_DELETE_ON_CLOSE) {
        if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
        if (open.isDir && !isEmptyDir(*fs, path))
          return STATUS_DIRECTORY_NOT_EMPTY;
        open.deleteOnClose = true;
      }
    }
    open.id = nextFileId++;
    c.opens.push_back(open);
    req.relatedFileId = open.id;
    req.relatedStatus = STATUS_SUCCESS;

    uint64_t t = toFileTime(info.modified);
    Writer w(resp);
    w.u16(89);
    w.u8(0);  // no oplock
    w.u8(0);
    w.u32(action);
    w.u64(t);
    w.u64(t);
    w.u64(t);
    w.u64(t);
    w.u64(allocationSize(info.size));
    w.u64(info.size);
    w.u32(open.isPipe ? FILE_ATTRIBUTE_NORMAL : attributes(info));
    w.u32(0);
    w.u64(open.id);
    w.u64(open.id);
    w.u32(0);  // no create contexts
    w.u32(0);
    w.u8(0);
    SMB_LOGD("open %s", open.path.c_str());
    return STATUS_SUCCESS;
  }

  static bool isEmptyDir(FileSystem& fs, const std::string& path) {
    bool empty = true;
    fs.listDir(path.c_str(), [&](const FileInfo&) {
      empty = false;
      return false;
    });
    return empty;
  }

  bool parentExists(const Share& share, const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == 0) return true;
    FileInfo info;
    std::string parent = fsPath(share, path.substr(0, pos));
    return share.fs->stat(parent.c_str(), info) && info.isDirectory;
  }

  /// Closes an open and removes it from the list
  void closeOpen(Connection& c, size_t idx) {
    Open o = c.opens[idx];
    c.opens.erase(c.opens.begin() + idx);
    if (o.isPipe) return;
    Tree* tree = nullptr;
    for (auto& t : c.trees)
      if (t.id == o.treeId) tree = &t;
    if (tree == nullptr || tree->share < 0) return;
    Share& share = shares[tree->share];
    std::string path = fsPath(share, o.path);
    bool stillOpen = false;
    for (auto& other : c.opens)
      if (other.treeId == o.treeId && other.path == o.path) stillOpen = true;
    uint32_t closeStartUs = micros();
    if (!stillOpen) share.fs->close(path.c_str());
    // closing flushes the written data
    if (timingLog && (o.timing.reads || o.timing.writes))
      logTiming(o, "done", micros() - closeStartUs);
    if (o.deleteOnClose) {
      bool ok = o.isDir ? share.fs->rmdir(path.c_str())
                        : share.fs->remove(path.c_str());
      SMB_LOGI("delete %s: %s", path.c_str(), ok ? "ok" : "failed");
    }
  }

  void closeAll(Connection& c) {
    while (!c.opens.empty()) closeOpen(c, c.opens.size() - 1);
  }

  uint32_t onClose(Connection& c, Request& req) {
    Open* o = findOpen(c, req, 72);
    if (o == nullptr) return fileStatus(req);
    bool postQuery = req.r.u16(66) & 1;
    FileInfo info;
    bool hasInfo = postQuery && infoOf(req, *o, info);
    closeOpen(c, o - c.opens.data());
    Writer w(resp);
    w.u16(60);
    w.u16(hasInfo ? 1 : 0);
    w.u32(0);
    if (hasInfo) {
      uint64_t t = toFileTime(info.modified);
      for (int i = 0; i < 4; i++) w.u64(t);
      w.u64(allocationSize(info.size));
      w.u64(info.size);
      w.u32(attributes(info));
    } else {
      w.zeros(52);
    }
    return STATUS_SUCCESS;
  }

  uint32_t onFlush(Connection& c, Request& req) {
    Open* o = findOpen(c, req, 72);
    if (o == nullptr) return fileStatus(req);
    Share* share = shareOf(req);
    if (share != nullptr) share->fs->flush(fsPath(*share, o->path).c_str());
    return body16(4);
  }

  uint32_t onRead(Connection& c, Request& req) {
    uint32_t length = req.r.u32(68);
    uint64_t offset = req.r.u64(72);
    Open* o = findOpen(c, req, 80);
    if (o == nullptr) return fileStatus(req);
    if (o->isDir) return STATUS_INVALID_DEVICE_REQUEST;
    if (length > maxIO) length = maxIO;

    const size_t dataOff = SMB2_HEADER_SIZE + 16;
    resp.resize(dataOff);
    uint32_t status = STATUS_SUCCESS;
    int64_t n;
    if (o->isPipe) {
      n = o->pipeOut.size() < length ? o->pipeOut.size() : length;
      resp.insert(resp.end(), o->pipeOut.begin(), o->pipeOut.begin() + n);
      o->pipeOut.erase(o->pipeOut.begin(), o->pipeOut.begin() + n);
      if (!o->pipeOut.empty()) status = STATUS_BUFFER_OVERFLOW;
    } else {
      Share* share = shareOf(req);
      resp.resize(dataOff + length);
      uint32_t startUs = micros();
      n = share->fs->read(fsPath(*share, o->path).c_str(), offset,
                          resp.data() + dataOff, length);
      if (timingLog) timeStorage(c, *o, startUs, true, n);
      if (n < 0) {
        resp.resize(SMB2_HEADER_SIZE);
        return STATUS_ACCESS_DENIED;
      }
      resp.resize(dataOff + n);
      if (n == 0 && length > 0) {
        resp.resize(SMB2_HEADER_SIZE);
        return STATUS_END_OF_FILE;
      }
    }
    Writer w(resp);
    w.put16(SMB2_HEADER_SIZE, 17);
    resp[SMB2_HEADER_SIZE + 2] = (uint8_t)dataOff;
    resp[SMB2_HEADER_SIZE + 3] = 0;
    w.put32(SMB2_HEADER_SIZE + 4, (uint32_t)n);
    w.put32(SMB2_HEADER_SIZE + 8, 0);
    w.put32(SMB2_HEADER_SIZE + 12, 0);
    if (n == 0) w.u8(0);
    return status;
  }

  uint32_t onWrite(Connection& c, Request& req) {
    uint16_t dataOff = req.r.u16(66);
    uint32_t length = req.r.u32(68);
    uint64_t offset = req.r.u64(72);
    Open* o = findOpen(c, req, 80);
    if (o == nullptr) return fileStatus(req);
    const uint8_t* data = req.r.ptr(dataOff, length);
    if (data == nullptr) return STATUS_INVALID_PARAMETER;
    if (o->isDir) return STATUS_INVALID_DEVICE_REQUEST;
    int64_t n = length;
    if (o->isPipe) {
      SrvSvc::process(data, length, shareEntries(), serverName, o->pipeOut);
    } else {
      Share* share = shareOf(req);
      if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
      uint32_t startUs = micros();
      n = share->fs->write(fsPath(*share, o->path).c_str(), offset, data,
                           length);
      if (timingLog) timeStorage(c, *o, startUs, false, n);
      if (n < 0) return STATUS_DISK_FULL;
    }
    Writer w(resp);
    w.u16(17);
    w.u16(0);
    w.u32((uint32_t)n);
    w.u32(0);
    w.u16(0);
    w.u16(0);
    w.u8(0);
    return STATUS_SUCCESS;
  }

  std::vector<ShareEntry> shareEntries() {
    std::vector<ShareEntry> result;
    for (auto& s : shares) result.push_back({s.name, 0, s.comment});
    result.push_back({"IPC$", 0x80000003, "IPC Service"});
    return result;
  }

  uint32_t onIoctl(Connection& c, Request& req) {
    uint32_t code = req.r.u32(68);
    if (code != FSCTL_PIPE_TRANSCEIVE) {
      if (code == FSCTL_DFS_GET_REFERRALS || code == 0x000601B0)
        return STATUS_FS_DRIVER_REQUIRED;
      return STATUS_NOT_SUPPORTED;
    }
    Open* o = findOpen(c, req, 72);
    if (o == nullptr) return fileStatus(req);
    if (!o->isPipe) return STATUS_INVALID_DEVICE_REQUEST;
    uint32_t inOff = req.r.u32(88);
    uint32_t inLen = req.r.u32(92);
    uint32_t maxOut = req.r.u32(108);
    const uint8_t* in = req.r.ptr(inOff, inLen);
    if (in == nullptr) return STATUS_INVALID_PARAMETER;
    SrvSvc::process(in, inLen, shareEntries(), serverName, o->pipeOut);
    size_t n = o->pipeOut.size() < maxOut ? o->pipeOut.size() : maxOut;
    Writer w(resp);
    w.u16(49);
    w.u16(0);
    w.u32(code);
    w.u64(o->id);
    w.u64(o->id);
    w.u32(SMB2_HEADER_SIZE + 48);  // input offset
    w.u32(0);
    w.u32(SMB2_HEADER_SIZE + 48);  // output offset
    w.u32((uint32_t)n);
    w.u32(0);
    w.u32(0);
    w.bytes(o->pipeOut.data(), n);
    o->pipeOut.erase(o->pipeOut.begin(), o->pipeOut.begin() + n);
    return o->pipeOut.empty() ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
  }

  // ------------------------------------------------------- query directory

  static bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
      if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
        return false;
    return true;
  }

  /// Case insensitive wildcard matching (*, ? and the DOS wildcards)
  static bool match(const char* p, const char* s) {
    while (*p) {
      char pc = *p;
      if (pc == '*' || pc == '<') {
        while (*p == '*' || *p == '<') p++;
        if (!*p) return true;
        for (; *s; s++)
          if (match(p, s)) return true;
        return match(p, s);
      }
      if (!*s) {
        // DOS_DOT and DOS_QM match the end of the name
        if (pc == '"' || pc == '>') {
          p++;
          continue;
        }
        return false;
      }
      if (pc != '?' && pc != '>' &&
          !(pc == '"' && *s == '.') &&
          tolower((unsigned char)pc) != tolower((unsigned char)*s))
        return false;
      p++;
      s++;
    }
    return *s == 0;
  }

  static bool hasWildcards(const std::string& p) {
    return p.find_first_of("*?<>\"") != std::string::npos;
  }

  /// Writes a single directory entry; returns the start of the entry
  template <class Vec>
  void writeDirEntry(Writer<Vec>& w, uint8_t infoClass, const FileInfo& info,
                     const std::string& dirPath) {
    uint64_t t = toFileTime(info.modified);
    std::vector<uint8_t> name;
    Writer nw(name);
    nw.utf16(info.name);
    w.u32(0);  // next entry offset
    w.u32(0);  // file index
    if (infoClass == FileNamesInformation) {
      w.u32((uint32_t)name.size());
      w.bytes(name.data(), name.size());
      return;
    }
    for (int i = 0; i < 4; i++) w.u64(t);
    w.u64(info.size);
    w.u64(allocationSize(info.size));
    w.u32(attributes(info));
    w.u32((uint32_t)name.size());
    if (infoClass != FileDirectoryInformation) w.u32(0);  // EA size
    if (infoClass == FileBothDirectoryInformation ||
        infoClass == FileIdBothDirectoryInformation) {
      w.u8(0);  // short name length
      w.u8(0);
      w.zeros(24);
    }
    if (infoClass == FileIdBothDirectoryInformation) w.u16(0);
    if (infoClass == FileIdFullDirectoryInformation) w.u32(0);
    if (infoClass == FileIdBothDirectoryInformation ||
        infoClass == FileIdFullDirectoryInformation) {
      std::string p = dirPath == "/" ? "/" + info.name
                                     : dirPath + "/" + info.name;
      w.u64(fileIndex(p));
    }
    w.bytes(name.data(), name.size());
  }

  uint32_t onQueryDirectory(Connection& c, Request& req) {
    uint8_t infoClass = req.r.u8(66);
    uint8_t flags = req.r.u8(67);
    Open* o = findOpen(c, req, 72);
    if (o == nullptr) return fileStatus(req);
    std::string pattern = req.r.utf16(req.r.u16(88), req.r.u16(90));
    uint32_t maxOut = req.r.u32(92);
    if (maxOut > maxIO) maxOut = maxIO;
    Share* share = shareOf(req);
    if (!o->isDir || share == nullptr) return STATUS_INVALID_PARAMETER;
    switch (infoClass) {
      case FileDirectoryInformation:
      case FileFullDirectoryInformation:
      case FileBothDirectoryInformation:
      case FileNamesInformation:
      case FileIdBothDirectoryInformation:
      case FileIdFullDirectoryInformation:
        break;
      default:
        return STATUS_INVALID_INFO_CLASS;
    }
    if (pattern.empty()) pattern = "*";
    if (pattern == "*.*") pattern = "*";
    bool restart = (flags & (SMB2_RESTART_SCANS | SMB2_REOPEN)) ||
                   o->pattern.empty();
    if (restart) {
      o->dirIndex = 0;
      o->pattern = pattern;
      o->dirListed = false;
      o->dirEntries.clear();
    }
    bool first = o->dirIndex == 0;
    bool single = flags & SMB2_RETURN_SINGLE_ENTRY;

    // Read the directory from storage only once per open and cache the
    // (pattern-matched) entries; subsequent QUERY_DIRECTORY requests for
    // the same open just page through the cache. Without this, clients
    // that page large directories in several requests would cause an
    // O(n^2) re-scan of storage (very slow on SD/FatFs).
    std::string path = fsPath(*share, o->path);
    if (!o->dirListed) {
      o->dirListed = true;
      if (!hasWildcards(o->pattern)) {
        // lookup of a single name
        FileInfo info;
        std::string p = (path == "/" ? "" : path) + "/" + o->pattern;
        if (share->fs->stat(p.c_str(), info)) {
          info.name = o->pattern;
          o->dirEntries.push_back(info);
        }
      } else {
        FileInfo dot;
        dot.isDirectory = true;
        dot.name = ".";
        o->dirEntries.push_back(dot);
        dot.name = "..";
        o->dirEntries.push_back(dot);
        share->fs->listDir(path.c_str(), [&](const FileInfo& info) {
          if (match(o->pattern.c_str(), info.name.c_str()))
            o->dirEntries.push_back(info);
          return true;
        });
      }
    }

    Writer w(resp);
    w.u16(9);
    w.u16(SMB2_HEADER_SIZE + 8);
    w.u32(0);  // length: patched
    const size_t start = resp.size();
    size_t lastEntry = 0;
    int added = 0;
    bool full = false;
    std::string dirPath = o->path;

    while (o->dirIndex < o->dirEntries.size()) {
      const FileInfo& info = o->dirEntries[o->dirIndex];
      size_t before = resp.size();
      if (added > 0) w.align(8, start);
      size_t entry = resp.size();
      writeDirEntry(w, infoClass, info, dirPath);
      if (resp.size() - start > maxOut) {
        resp.resize(before);
        full = true;
        break;
      }
      if (added > 0) w.put32(lastEntry, (uint32_t)(entry - lastEntry));
      lastEntry = entry;
      added++;
      o->dirIndex++;
      if (single) break;
    }

    if (added == 0) {
      if (full) return STATUS_INFO_LENGTH_MISMATCH;
      return first ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
    }
    w.put32(SMB2_HEADER_SIZE + 4, (uint32_t)(resp.size() - start));
    return STATUS_SUCCESS;
  }

  // ------------------------------------------------------------ query info

  template <class Vec>
  void writeBasicInfo(Writer<Vec>& w, const FileInfo& info) {
    uint64_t t = toFileTime(info.modified);
    for (int i = 0; i < 4; i++) w.u64(t);
    w.u32(attributes(info));
    w.u32(0);
  }

  template <class Vec>
  void writeStandardInfo(Writer<Vec>& w, const FileInfo& info, bool deletePending) {
    w.u64(allocationSize(info.size));
    w.u64(info.size);
    w.u32(1);
    w.u8(deletePending ? 1 : 0);
    w.u8(info.isDirectory ? 1 : 0);
    w.u16(0);
  }

  static std::string windowsPath(const std::string& path) {
    std::string result = path;
    for (auto& ch : result)
      if (ch == '/') ch = '\\';
    return result;
  }

  void writeSecurityDescriptor(Writer<PsramVector<uint8_t>>& w, bool isDir) {
    const uint8_t everyone[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    w.u8(1);  // revision
    w.u8(0);
    w.u16(0x8004);  // self relative + DACL present
    w.u32(20);      // owner
    w.u32(32);      // group
    w.u32(0);       // SACL
    w.u32(44);      // DACL
    w.bytes(everyone, 12);
    w.bytes(everyone, 12);
    w.u8(2);  // ACL revision
    w.u8(0);
    w.u16(28);
    w.u16(1);
    w.u16(0);
    w.u8(0);  // ACCESS_ALLOWED_ACE
    w.u8(isDir ? 0x03 : 0x00);
    w.u16(20);
    w.u32(0x001F01FF);
    w.bytes(everyone, 12);
  }

  uint32_t onQueryInfo(Connection& c, Request& req) {
    uint8_t infoType = req.r.u8(66);
    uint8_t infoClass = req.r.u8(67);
    uint32_t maxOut = req.r.u32(68);
    Open* o = findOpen(c, req, 88);
    if (o == nullptr) return fileStatus(req);
    FileInfo info;
    if (!infoOf(req, *o, info)) return STATUS_OBJECT_NAME_NOT_FOUND;
    Share* share = shareOf(req);

    Writer w(resp);
    w.u16(9);
    w.u16(SMB2_HEADER_SIZE + 8);
    w.u32(0);
    const size_t start = resp.size();
    bool variable = false;
    uint32_t status = STATUS_SUCCESS;

    if (infoType == SMB2_0_INFO_FILE) {
      std::string name = windowsPath(o->path);
      switch (infoClass) {
        case FileBasicInformation:
          writeBasicInfo(w, info);
          break;
        case FileStandardInformation:
          writeStandardInfo(w, info, o->deleteOnClose);
          break;
        case FileInternalInformation:
          w.u64(fileIndex(o->path));
          break;
        case FileEaInformation:
        case FileModeInformation:
        case FileAlignmentInformation:
          w.u32(0);
          break;
        case FileFullEaInformation:
          // no extended attributes: an empty FILE_FULL_EA_INFORMATION list
          // is represented by a response with zero length (no entries)
          break;
        case FileAccessInformation:
          w.u32(0x001F01FF);
          break;
        case FilePositionInformation:
          w.u64(0);
          break;
        case FileAllInformation: {
          writeBasicInfo(w, info);
          writeStandardInfo(w, info, o->deleteOnClose);
          w.u64(fileIndex(o->path));
          w.u32(0);           // EA
          w.u32(0x001F01FF);  // access
          w.u64(0);           // position
          w.u32(0);           // mode
          w.u32(0);           // alignment
          size_t lenPos = w.pos();
          w.u32(0);
          w.put32(lenPos, (uint32_t)w.utf16(name));
          variable = true;
          break;
        }
        case FileNetworkOpenInformation: {
          uint64_t t = toFileTime(info.modified);
          for (int i = 0; i < 4; i++) w.u64(t);
          w.u64(allocationSize(info.size));
          w.u64(info.size);
          w.u32(attributes(info));
          w.u32(0);
          break;
        }
        case FileAttributeTagInformation:
          w.u32(attributes(info));
          w.u32(0);
          break;
        case FileStreamInformation:
          if (!info.isDirectory) {
            w.u32(0);
            size_t lenPos = w.pos();
            w.u32(0);
            w.u64(info.size);
            w.u64(allocationSize(info.size));
            w.put32(lenPos, (uint32_t)w.utf16("::$DATA"));
          }
          variable = true;
          break;
        case FileCompressionInformation:
          w.u64(info.size);
          w.u16(0);
          w.u8(0);
          w.u8(0);
          w.u8(0);
          w.zeros(3);
          break;
        case FileNormalizedNameInformation: {
          size_t lenPos = w.pos();
          w.u32(0);
          std::string n = name.size() > 1 ? name.substr(1) : "";
          w.put32(lenPos, (uint32_t)w.utf16(n));
          variable = true;
          break;
        }
        default:
          return STATUS_INVALID_INFO_CLASS;
      }
    } else if (infoType == SMB2_0_INFO_FILESYSTEM) {
      uint64_t total = 0, free = 0;
      if (share == nullptr || !share->fs->space(total, free)) {
        total = 1ULL << 30;
        free = 1ULL << 29;
      }
      const uint32_t clusterSize = 4096;
      switch (infoClass) {
        case FileFsVolumeInformation: {
          w.u64(startTime);
          w.u32((uint32_t)fileIndex(share ? share->name : "IPC$"));
          size_t lenPos = w.pos();
          w.u32(0);
          w.u8(0);
          w.u8(0);
          w.put32(lenPos, (uint32_t)w.utf16(share ? share->name : "IPC$"));
          variable = true;
          break;
        }
        case FileFsSizeInformation:
          w.u64(total / clusterSize);
          w.u64(free / clusterSize);
          w.u32(clusterSize / 512);
          w.u32(512);
          break;
        case FileFsFullSizeInformation:
          w.u64(total / clusterSize);
          w.u64(free / clusterSize);
          w.u64(free / clusterSize);
          w.u32(clusterSize / 512);
          w.u32(512);
          break;
        case FileFsDeviceInformation:
          w.u32(0x00000007);  // FILE_DEVICE_DISK
          w.u32(0x00000020);  // FILE_DEVICE_IS_MOUNTED
          break;
        case FileFsAttributeInformation: {
          bool cs = share != nullptr && share->fs->isCaseSensitive();
          // case preserved names + unicode on disk (+ case sensitive search)
          w.u32(0x00000006 | (cs ? 1 : 0));
          w.u32(255);
          size_t lenPos = w.pos();
          w.u32(0);
          w.put32(lenPos,
                  (uint32_t)w.utf16(share ? share->fs->name() : "NTFS"));
          variable = true;
          break;
        }
        case FileFsSectorSizeInformation:
          for (int i = 0; i < 4; i++) w.u32(512);
          w.u32(0x0000000B);  // aligned device, partition aligned...
          w.u32(0);
          w.u32(0);
          break;
        case FileFsObjectIdInformation:
          w.zeros(64);
          break;
        default:
          return STATUS_INVALID_INFO_CLASS;
      }
    } else if (infoType == SMB2_0_INFO_SECURITY) {
      writeSecurityDescriptor(w, info.isDirectory);
      if (resp.size() - start > maxOut) {
        // the client needs to retry with a bigger buffer
        uint32_t needed = (uint32_t)(resp.size() - start);
        resp.resize(SMB2_HEADER_SIZE);
        Writer e(resp);
        e.u16(9);
        e.u16(0);
        e.u32(4);
        e.u32(needed);
        keepErrorBody = true;
        return STATUS_BUFFER_TOO_SMALL;
      }
    } else {
      return STATUS_NOT_SUPPORTED;
    }

    size_t len = resp.size() - start;
    if (len > maxOut) {
      if (!variable) {
        return STATUS_INFO_LENGTH_MISMATCH;
      }
      resp.resize(start + maxOut);
      len = maxOut;
      status = STATUS_BUFFER_OVERFLOW;
    }
    w.put32(SMB2_HEADER_SIZE + 4, (uint32_t)len);
    return status;
  }

  // -------------------------------------------------------------- set info

  uint32_t onSetInfo(Connection& c, Request& req) {
    uint8_t infoType = req.r.u8(66);
    uint8_t infoClass = req.r.u8(67);
    uint32_t len = req.r.u32(68);
    uint16_t off = req.r.u16(72);
    Open* o = findOpen(c, req, 80);
    if (o == nullptr) return fileStatus(req);
    const uint8_t* data = req.r.ptr(off, len);
    if (data == nullptr) return STATUS_INVALID_PARAMETER;
    Reader in(data, len);
    Share* share = shareOf(req);
    if (infoType == SMB2_0_INFO_SECURITY) return body16(2);
    if (infoType != SMB2_0_INFO_FILE || share == nullptr)
      return STATUS_NOT_SUPPORTED;
    std::string path = fsPath(*share, o->path);
    FileSystem* fs = share->fs;

    switch (infoClass) {
      case FileBasicInformation:
      case FileAllocationInformation:
        break;  // timestamps and preallocation are ignored
      case FileEndOfFileInformation:
        if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
        if (o->isDir) return STATUS_INVALID_PARAMETER;
        if (!fs->truncate(path.c_str(), in.u64(0))) return STATUS_DISK_FULL;
        break;
      case FileDispositionInformation:
      case FileDispositionInformationEx: {
        bool del = infoClass == FileDispositionInformation
                       ? in.u8(0) != 0
                       : (in.u32(0) & 1) != 0;
        if (del) {
          if (req.tree->readOnly || o->path == "/")
            return STATUS_ACCESS_DENIED;
          if (o->isDir && !isEmptyDir(*fs, path))
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
        o->deleteOnClose = del;
        break;
      }
      case FileRenameInformation: {
        if (req.tree->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
        bool replace = in.u8(0) != 0;
        uint32_t nameLen = in.u32(16);
        std::string target;
        if (!toPath(in.utf16(20, nameLen), target) || in.hasError())
          return STATUS_OBJECT_NAME_INVALID;
        if (target == "/") return STATUS_ACCESS_DENIED;
        std::string to = fsPath(*share, target);
        FileInfo existing;
        if (fs->stat(to.c_str(), existing)) {
          bool sameFile = equalsIgnoreCase(target, o->path);
          if (!sameFile) {
            if (!replace || existing.isDirectory)
              return STATUS_OBJECT_NAME_COLLISION;
            fs->close(to.c_str());
            if (!fs->remove(to.c_str())) return STATUS_ACCESS_DENIED;
          }
        }
        if (!parentExists(*share, target)) return STATUS_OBJECT_PATH_NOT_FOUND;
        fs->close(path.c_str());
        if (!fs->rename(path.c_str(), to.c_str())) return STATUS_ACCESS_DENIED;
        // update all opens which refer to the old path
        std::string old = o->path;
        for (auto& other : c.opens) {
          if (other.treeId != o->treeId) continue;
          if (other.path == old) {
            other.path = target;
          } else if (other.path.rfind(old + "/", 0) == 0) {
            other.path = target + other.path.substr(old.size());
          }
        }
        SMB_LOGI("rename %s -> %s", path.c_str(), to.c_str());
        break;
      }
      default:
        return STATUS_INVALID_INFO_CLASS;
    }
    return body16(2);
  }
};

}  // namespace smb
