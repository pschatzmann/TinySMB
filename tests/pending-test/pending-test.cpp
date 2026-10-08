// Tests the handling of interim STATUS_PENDING responses by the SMBClient with
// a scripted server: CREATE is answered with an unsigned interim response and
// the signed final (async) response arrives after a configurable delay.
#include <stdio.h>

#include <deque>

#include "SMB.h"

static int failures = 0;

static void check(const char* name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) failures++;
}

/// Arduino Client which simulates a SMB2 server
class MockServer : public Client {
 public:
  uint32_t pendingDelay = 0;      ///< delay of the final CREATE response (ms)
  bool corruptSignature = false;  ///< invalid signature of the final CREATE
  int pendingCount = 0;

  int connect(IPAddress, uint16_t) override { return connect("", 0); }
  int connect(const char*, uint16_t) override {
    isConnected = true;
    return 1;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t size) override {
    if (!isConnected) return 0;
    in.insert(in.end(), buf, buf + size);
    while (in.size() >= 4) {
      size_t len = ((size_t)in[1] << 16) | ((size_t)in[2] << 8) | in[3];
      if (in.size() < len + 4) break;
      std::vector<uint8_t> msg(in.begin() + 4, in.begin() + 4 + len);
      in.erase(in.begin(), in.begin() + 4 + len);
      process(msg);
    }
    return size;
  }
  int available() override {
    while (!out.empty() && out.front().readyAt <= millis()) {
      auto& c = out.front();
      readable.insert(readable.end(), c.data.begin(), c.data.end());
      out.pop_front();
    }
    return (int)readable.size();
  }
  int read() override {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  int read(uint8_t* buf, size_t size) override {
    size_t n = available() < (int)size ? available() : size;
    memcpy(buf, readable.data(), n);
    readable.erase(readable.begin(), readable.begin() + n);
    return (int)n;
  }
  int peek() override { return available() ? readable[0] : -1; }
  void flush() override {}
  void stop() override { isConnected = false; }
  uint8_t connected() override { return isConnected; }
  operator bool() override { return isConnected; }

 protected:
  struct Chunk {
    uint32_t readyAt;
    std::vector<uint8_t> data;
  };
  bool isConnected = false;
  std::vector<uint8_t> in, readable;
  std::deque<Chunk> out;
  NtlmAuth auth;
  std::vector<User> users = {{"user", "password"}};
  bool signing = false;
  uint8_t key[16];
  uint64_t asyncId = 1;

  /// Response header: async responses have an AsyncId instead of the tree id
  std::vector<uint8_t> header(const Reader& r, uint32_t status, bool async) {
    std::vector<uint8_t> h(SMB2_HEADER_SIZE, 0);
    Writer w(h);
    h[0] = 0xFE;
    h[1] = 'S';
    h[2] = 'M';
    h[3] = 'B';
    w.put16(4, 64);
    w.put32(8, status);
    w.put16(12, r.u16(12));
    w.put16(14, 1);
    w.put32(16, SMB2_FLAGS_SERVER_TO_REDIR | (async ? SMB2_FLAGS_ASYNC_COMMAND : 0));
    w.put64(24, r.u64(24));
    if (async) {
      w.put64(32, asyncId);
    } else {
      w.put32(36, r.u32(36) ? r.u32(36) : 7);
    }
    w.put64(40, 0x1234);
    return h;
  }

  void send(std::vector<uint8_t> msg, uint32_t delay = 0, bool sign = true,
            bool corrupt = false) {
    if (sign && signing) {
      Writer w(msg);
      w.put32(16, Reader(msg.data(), msg.size()).u32(16) | SMB2_FLAGS_SIGNED);
      uint8_t mac[32];
      Crypto::hmacSha256(key, 16, msg.data(), msg.size(), nullptr, 0, mac);
      memcpy(msg.data() + 48, mac, 16);
      if (corrupt) msg[48] ^= 0xFF;
    }
    size_t len = msg.size();
    std::vector<uint8_t> frame = {0, (uint8_t)(len >> 16), (uint8_t)(len >> 8),
                                  (uint8_t)len};
    frame.insert(frame.end(), msg.begin(), msg.end());
    out.push_back({(uint32_t)millis() + delay, frame});
  }

  void process(const std::vector<uint8_t>& msg) {
    Reader r(msg.data(), msg.size());
    std::vector<uint8_t> resp;
    switch (r.u16(12)) {
      case SMB2_NEGOTIATE: {
        resp = header(r, STATUS_SUCCESS, false);
        Writer w(resp);
        w.u16(65);
        w.u16(SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED);
        w.u16(SMB2_DIALECT_0210);
        w.u16(0);
        w.zeros(16);
        w.u32(0);
        for (int i = 0; i < 3; i++) w.u32(65536);
        w.u64(0);
        w.u64(0);
        w.u16(0);
        w.u16(0);
        w.u32(0);
        send(resp);
        break;
      }
      case SMB2_SESSION_SETUP: {
        std::vector<uint8_t> token;
        AuthResult rc = auth.process(r.ptr(r.u16(76), r.u16(78)), r.u16(78),
                                     users, false, token);
        uint32_t status = rc == AuthResult::Continue
                              ? STATUS_MORE_PROCESSING_REQUIRED
                              : rc == AuthResult::Success ? STATUS_SUCCESS
                                                          : STATUS_LOGON_FAILURE;
        resp = header(r, status, false);
        Writer w(resp);
        w.u16(9);
        w.u16(0);
        w.u16(SMB2_HEADER_SIZE + 8);
        w.u16((uint16_t)token.size());
        w.bytes(token.data(), token.size());
        send(resp);
        if (rc == AuthResult::Success) {
          memcpy(key, auth.sessionKey(), 16);
          signing = true;
        }
        break;
      }
      case SMB2_TREE_CONNECT: {
        resp = header(r, STATUS_SUCCESS, false);
        Writer w(resp);
        w.u16(16);
        w.u8(SMB2_SHARE_TYPE_DISK);
        w.u8(0);
        w.u32(0);
        w.u32(0);
        w.u32(0x001F01FF);
        send(resp);
        break;
      }
      case SMB2_CREATE: {
        // interim response: unsigned, no body except the error structure
        pendingCount++;
        resp = header(r, 0x00000103, true);
        Writer e(resp);
        e.u16(9);
        e.u16(0);
        e.u32(0);
        e.u8(0);
        send(resp, 0, false);
        // final response
        resp = header(r, STATUS_SUCCESS, true);
        Writer w(resp);
        w.u16(89);
        w.u8(0);
        w.u8(0);
        w.u32(FILE_OPENED);
        for (int i = 0; i < 4; i++) w.u64(toFileTime(1700000000));
        w.u64(4096);
        w.u64(1234);  // size
        w.u32(FILE_ATTRIBUTE_ARCHIVE);
        w.u32(0);
        w.u64(11);
        w.u64(22);
        w.u32(0);
        w.u32(0);
        w.u8(0);
        send(resp, pendingDelay, true, corruptSignature);
        asyncId++;
        break;
      }
      default: {
        // CLOSE and all others: success
        resp = header(r, STATUS_SUCCESS, false);
        Writer w(resp);
        w.u16(r.u16(12) == SMB2_CLOSE ? 60 : 4);
        w.zeros(r.u16(12) == SMB2_CLOSE ? 58 : 2);
        send(resp);
      }
    }
  }
};

/// Connects with short timeouts and calls stat() on a file
static bool runStat(uint32_t pendingDelay, bool corrupt, FileInfo& info,
                    MockServer& server, uint32_t& status) {
  server.pendingDelay = pendingDelay;
  server.corruptSignature = corrupt;
  SMBClient client(server);
  client.setTimeout(200);
  client.setPendingTimeout(2000);
  status = 0xFFFFFFFF;
  if (!client.begin("mock", "share", "user", "password")) return false;
  bool ok = client.stat("/file.txt", info);
  status = client.lastStatus();
  return ok;
}

int main() {
  FileInfo info;
  uint32_t status;
  {
    MockServer server;
    bool ok = runStat(0, false, info, server, status);
    check("immediate final response", ok && info.size == 1234 &&
                                          server.pendingCount == 1);
  }
  {
    // final response arrives after the normal timeout (200 ms), but within
    // the pending timeout (2000 ms)
    MockServer server;
    uint32_t start = millis();
    bool ok = runStat(800, false, info, server, status);
    check("delayed final response", ok && info.size == 1234 &&
                                        info.modified == 1700000000 &&
                                        millis() - start >= 800);
  }
  {
    MockServer server;
    bool ok = runStat(3000, false, info, server, status);
    check("final response after the pending timeout fails", !ok);
  }
  {
    MockServer server;
    bool ok = runStat(0, true, info, server, status);
    check("invalid signature of the final response fails",
          !ok && status == STATUS_ACCESS_DENIED);
  }
  printf("%s\n", failures ? "FAILED" : "ALL TESTS PASSED");
  return failures ? 1 : 0;
}
