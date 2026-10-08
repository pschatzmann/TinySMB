#pragma once
#include <ctype.h>

#include <string>
#include <vector>

#include "SMBBuffer.h"

namespace smb {

/// Share description used by the RPC share enumeration
struct ShareEntry {
  std::string name;
  uint32_t type;  // 0 = disk, 0x80000003 = IPC$
  std::string comment;
};

/**
 * @brief Minimal DCE/RPC implementation of the srvsvc named pipe which is
 * used by clients to enumerate the shares (NetShareEnumAll), query a share
 * (NetShareGetInfo) and the server (NetServerGetInfo).
 */
class SrvSvc {
 public:
  /// Processes a DCE/RPC request PDU; returns false if no response is needed
  static bool process(const uint8_t* data, size_t len,
                      const std::vector<ShareEntry>& shares,
                      const std::string& serverName,
                      std::vector<uint8_t>& out) {
    out.clear();
    Reader r(data, len);
    if (len < 16 || r.u8(0) != 5) return false;
    uint8_t type = r.u8(2);
    uint32_t callId = r.u32(12);
    if (type == 11) return bindAck(r, callId, out);
    if (type != 0) return false;

    uint16_t ctxId = r.u16(20);
    uint16_t opnum = r.u16(22);
    size_t stub = (r.u8(3) & 0x80) ? 40 : 24;  // object uuid present
    Reader in(data + (stub < len ? stub : len), stub < len ? len - stub : 0);

    std::vector<uint8_t> body;
    Writer w(body);
    bool ok = true;
    switch (opnum) {
      case 15:
        ok = netShareEnumAll(in, shares, w);
        break;
      case 16:
        ok = netShareGetInfo(in, shares, w);
        break;
      case 21:
        ok = netServerGetInfo(in, serverName, w);
        break;
      default:
        ok = false;
    }
    if (!ok) {
      fault(callId, ctxId, out);
      return true;
    }
    Writer o(out);
    header(o, 2, callId);
    o.u32((uint32_t)body.size());  // alloc hint
    o.u16(ctxId);
    o.u8(0);  // cancel count
    o.u8(0);
    o.bytes(body.data(), body.size());
    o.put16(8, (uint16_t)out.size());
    return true;
  }

 protected:
  static void header(Writer<>& w, uint8_t type, uint32_t callId) {
    w.u8(5);
    w.u8(0);
    w.u8(type);
    w.u8(0x03);  // first + last fragment
    w.u32(0x00000010);  // little endian, ASCII, IEEE float
    w.u16(0);  // frag length: patched
    w.u16(0);  // auth length
    w.u32(callId);
  }

  static bool bindAck(const Reader& r, uint32_t callId,
                      std::vector<uint8_t>& out) {
    static const uint8_t NDR[16] = {0x04, 0x5d, 0x88, 0x8a, 0xeb, 0x1c,
                                    0xc9, 0x11, 0x9f, 0xe8, 0x08, 0x00,
                                    0x2b, 0x10, 0x48, 0x60};
    uint16_t maxXmit = r.u16(16);
    uint16_t maxRecv = r.u16(18);
    uint8_t count = r.u8(24);
    Writer w(out);
    header(w, 12, callId);
    w.u16(maxXmit);
    w.u16(maxRecv);
    w.u32(0x12345);  // association group
    const char port[] = "\\PIPE\\srvsvc";
    w.u16(sizeof(port));
    w.bytes(port, sizeof(port));
    w.align(4);
    w.u8(count);
    w.u8(0);
    w.u16(0);
    size_t off = 28;
    for (int i = 0; i < count; i++) {
      uint8_t nTransfer = r.u8(off + 2);
      int accepted = -1;
      for (int t = 0; t < nTransfer; t++) {
        const uint8_t* uuid = r.ptr(off + 24 + t * 20, 16);
        if (uuid && memcmp(uuid, NDR, 16) == 0) accepted = t;
      }
      if (accepted >= 0) {
        w.u16(0);  // acceptance
        w.u16(0);
        w.bytes(r.ptr(off + 24 + accepted * 20, 20), 20);
      } else {
        w.u16(2);  // provider rejection
        w.u16(2);  // proposed transfer syntaxes not supported
        w.zeros(20);
      }
      off += 24 + nTransfer * 20;
    }
    if (r.hasError()) return false;
    w.put16(8, (uint16_t)out.size());
    return true;
  }

  static void fault(uint32_t callId, uint16_t ctxId,
                    std::vector<uint8_t>& out) {
    out.clear();
    Writer w(out);
    header(w, 3, callId);
    w.u32(32);
    w.u16(ctxId);
    w.u8(0);
    w.u8(0);
    w.u32(0x1c010002);  // nca_s_op_rng_error
    w.u32(0);
    w.put16(8, (uint16_t)out.size());
  }

  /// skips a [unique] wchar_t* argument and returns the next offset
  static size_t skipUniqueString(const Reader& r, size_t off) {
    uint32_t ref = r.u32(off);
    off += 4;
    if (ref == 0) return off;
    return skipString(r, off);
  }

  static size_t skipString(const Reader& r, size_t off) {
    uint32_t actual = r.u32(off + 8);
    off += 12 + actual * 2;
    return (off + 3) & ~(size_t)3;
  }

  static std::string readString(const Reader& r, size_t off) {
    uint32_t actual = r.u32(off + 8);
    std::string s = r.utf16(off + 12, actual * 2);
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
  }

  /// NDR conformant varying string (including the null terminator)
  static void string(Writer<>& w, const std::string& s) {
    std::vector<uint8_t> tmp;
    Writer t(tmp);
    size_t n = t.utf16(s, true) / 2;
    w.u32((uint32_t)n);
    w.u32(0);
    w.u32((uint32_t)n);
    w.bytes(tmp.data(), tmp.size());
    w.align(4);
  }

  static bool netShareEnumAll(const Reader& in,
                              const std::vector<ShareEntry>& shares,
                              Writer<>& w) {
    size_t off = skipUniqueString(in, 0);
    uint32_t level = in.u32(off);
    if (in.hasError()) return false;
    uint32_t ref = 0x20000;
    w.u32(level);
    w.u32(level);
    if (level != 0 && level != 1) {
      w.u32(0);  // null container
      w.u32(0);  // total entries
      w.u32(0);  // resume handle
      w.u32(124);  // WERR_INVALID_LEVEL
      return true;
    }
    uint32_t n = (uint32_t)shares.size();
    w.u32(ref++);  // container pointer
    w.u32(n);      // entries read
    w.u32(ref++);  // array pointer
    w.u32(n);      // array max count
    for (auto& s : shares) {
      w.u32(ref++);  // name pointer
      if (level == 1) {
        w.u32(s.type);
        w.u32(ref++);  // comment pointer
      }
    }
    for (auto& s : shares) {
      string(w, s.name);
      if (level == 1) string(w, s.comment);
    }
    w.u32(n);      // total entries
    w.u32(ref++);  // resume handle pointer
    w.u32(0);      // resume handle
    w.u32(0);      // WERR_OK
    return true;
  }

  static bool netShareGetInfo(const Reader& in,
                              const std::vector<ShareEntry>& shares,
                              Writer<>& w) {
    size_t off = skipUniqueString(in, 0);
    std::string name = readString(in, off);
    off = skipString(in, off);
    uint32_t level = in.u32(off);
    if (in.hasError()) return false;
    const ShareEntry* share = nullptr;
    for (auto& s : shares) {
      if (equalsIgnoreCase(s.name, name)) share = &s;
    }
    w.u32(level);
    if (share == nullptr || (level != 0 && level != 1 && level != 1005)) {
      w.u32(0);
      w.u32(share == nullptr ? 2310 : 124);  // NERR_NetNameNotFound
      return true;
    }
    w.u32(0x20000);
    if (level == 1005) {
      w.u32(0);  // flags
    } else {
      w.u32(0x20004);
      if (level == 1) {
        w.u32(share->type);
        w.u32(0x20008);
      }
      string(w, share->name);
      if (level == 1) string(w, share->comment);
    }
    w.u32(0);
    return true;
  }

  static bool netServerGetInfo(const Reader& in, const std::string& name,
                               Writer<>& w) {
    size_t off = skipUniqueString(in, 0);
    uint32_t level = in.u32(off);
    if (in.hasError()) return false;
    w.u32(level);
    if (level != 100 && level != 101) {
      w.u32(0);
      w.u32(124);
      return true;
    }
    w.u32(0x20000);
    w.u32(500);  // PLATFORM_ID_NT
    w.u32(0x20004);
    if (level == 101) {
      w.u32(6);           // major version
      w.u32(1);           // minor version
      w.u32(0x00009003);  // workstation, server, NT
      w.u32(0x20008);
    }
    string(w, name);
    if (level == 101) string(w, "");
    w.u32(0);
    return true;
  }

  static bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
      if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
        return false;
    }
    return true;
  }
};

}  // namespace smb
