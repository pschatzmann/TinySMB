#pragma once
#include <ctype.h>

#include <string>
#include <vector>

#include "../smb-server/SMBBuffer.h"
#include "../smb-server/SMBCrypto.h"
#include "../smb-server/SMBDefs.h"
#include "../smb-server/SMBLogger.h"
#include "../smb-server/SMBPlatform.h"

namespace smb {

/**
 * @brief Client side of NTLMSSP (NTLMv2) wrapped in SPNEGO for the SMB2
 * SESSION_SETUP. If the server provides a timestamp, the AUTHENTICATE
 * message contains a MIC and the final token a SPNEGO mechListMIC, as
 * required by Windows and Samba.
 */
class NtlmClient {
 public:
  static const uint32_t NEGOTIATE_UNICODE = 0x00000001;
  static const uint32_t REQUEST_TARGET = 0x00000004;
  static const uint32_t NEGOTIATE_SIGN = 0x00000010;
  static const uint32_t NEGOTIATE_NTLM = 0x00000200;
  static const uint32_t NEGOTIATE_ANONYMOUS = 0x00000800;
  static const uint32_t NEGOTIATE_ALWAYS_SIGN = 0x00008000;
  static const uint32_t NEGOTIATE_EXTENDED_SESSIONSECURITY = 0x00080000;
  static const uint32_t NEGOTIATE_TARGET_INFO = 0x00800000;
  static const uint32_t NEGOTIATE_VERSION = 0x02000000;
  static const uint32_t NEGOTIATE_128 = 0x20000000;
  static const uint32_t NEGOTIATE_KEY_EXCH = 0x40000000;
  static const uint32_t NEGOTIATE_56 = 0x80000000;

  void setCredentials(const std::string& user, const std::string& password,
                      const std::string& domain) {
    this->user = user;
    this->password = password;
    this->domain = domain;
  }

  bool isAnonymous() const { return user.empty(); }

  /// First token: SPNEGO NegTokenInit with the NTLM NEGOTIATE message
  std::vector<uint8_t> negotiateToken() {
    negotiateMsg.clear();
    Writer w(negotiateMsg);
    w.bytes("NTLMSSP", 8);
    w.u32(1);
    w.u32(NEGOTIATE_UNICODE | REQUEST_TARGET | NEGOTIATE_SIGN |
          NEGOTIATE_NTLM | NEGOTIATE_ALWAYS_SIGN |
          NEGOTIATE_EXTENDED_SESSIONSECURITY | NEGOTIATE_VERSION |
          NEGOTIATE_128 | NEGOTIATE_KEY_EXCH | NEGOTIATE_56);
    w.zeros(16);  // domain and workstation fields
    writeVersion(w);

    std::vector<uint8_t> body = der(0xA0, mechTypes());
    std::vector<uint8_t> token = der(0xA2, der(0x04, negotiateMsg));
    body.insert(body.end(), token.begin(), token.end());
    std::vector<uint8_t> init = spnegoOid();
    std::vector<uint8_t> neg = der(0xA0, der(0x30, body));
    init.insert(init.end(), neg.begin(), neg.end());
    return der(0x60, init);
  }

  /// Processes the server challenge and provides the AUTHENTICATE token
  bool authenticateToken(const uint8_t* blob, size_t len,
                         std::vector<uint8_t>& out) {
    size_t pos = find(blob, len, "NTLMSSP", 8);
    if (pos == (size_t)-1) return false;
    Reader ch(blob + pos, len - pos);
    if (ch.u32(8) != 2) return false;
    uint32_t chLen = challengeLength(ch);
    challengeMsg.assign(blob + pos, blob + pos + chLen);
    Reader c(challengeMsg.data(), challengeMsg.size());
    flags = c.u32(20);
    const uint8_t* serverChallenge = c.ptr(24, 8);
    size_t infoLen = c.u16(40);
    const uint8_t* info = c.ptr(c.u32(44), infoLen);
    if (serverChallenge == nullptr || c.hasError()) return false;
    std::vector<uint8_t> targetInfo;
    if (info != nullptr) targetInfo.assign(info, info + infoLen);

    std::vector<uint8_t> auth;
    useMic = false;
    if (isAnonymous()) {
      buildAnonymous(auth);
    } else if (!buildAuthenticate(serverChallenge, targetInfo, auth)) {
      return false;
    }

    std::vector<uint8_t> seq = der(0xA2, der(0x04, auth));
    if (useMic) {
      std::vector<uint8_t> mic = der(0xA3, der(0x04, mechListMic()));
      seq.insert(seq.end(), mic.begin(), mic.end());
    }
    out = der(0xA1, der(0x30, seq));
    return true;
  }

  /// Exported session key: used to sign SMB2 messages
  const uint8_t* sessionKey() const { return exportedKey; }
  bool hasSessionKey() const { return !isAnonymous(); }

 protected:
  std::string user, password, domain;
  std::vector<uint8_t> negotiateMsg, challengeMsg;
  uint32_t flags = 0;
  uint8_t exportedKey[16] = {0};
  bool useMic = false;

  static void writeVersion(Writer& w) {
    const uint8_t version[8] = {10, 0, 0x61, 0x4A, 0, 0, 0, 15};
    w.bytes(version, 8);
  }

  static std::vector<uint8_t> spnegoOid() {
    return {0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
  }
  static std::vector<uint8_t> ntlmOid() {
    return {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04,
            0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
  }
  /// DER encoded MechTypeList (the mechListMIC is calculated over it)
  static std::vector<uint8_t> mechTypes() { return der(0x30, ntlmOid()); }

  static std::vector<uint8_t> der(uint8_t tag,
                                  const std::vector<uint8_t>& content) {
    std::vector<uint8_t> out;
    out.push_back(tag);
    size_t n = content.size();
    if (n < 0x80) {
      out.push_back((uint8_t)n);
    } else if (n < 0x100) {
      out.push_back(0x81);
      out.push_back((uint8_t)n);
    } else {
      out.push_back(0x82);
      out.push_back((uint8_t)(n >> 8));
      out.push_back((uint8_t)n);
    }
    out.insert(out.end(), content.begin(), content.end());
    return out;
  }

  static size_t find(const uint8_t* p, size_t len, const char* sig,
                     size_t sigLen) {
    for (size_t i = 0; i + sigLen <= len; i++)
      if (memcmp(p + i, sig, sigLen) == 0) return i;
    return (size_t)-1;
  }

  /// The CHALLENGE message ends with the last payload field
  static uint32_t challengeLength(const Reader& r) {
    uint32_t end = 56;
    uint32_t nameEnd = r.u32(16) + r.u16(12);
    uint32_t infoEnd = r.u32(44) + r.u16(40);
    if (nameEnd > end) end = nameEnd;
    if (infoEnd > end) end = infoEnd;
    return end > r.size() ? (uint32_t)r.size() : end;
  }

  static std::string upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)toupper((unsigned char)c);
    return r;
  }

  static void field(Writer& w, size_t at, size_t offset, size_t len) {
    w.put16(at, (uint16_t)len);
    w.put16(at + 2, (uint16_t)len);
    w.put32(at + 4, (uint32_t)offset);
  }

  void buildAnonymous(std::vector<uint8_t>& auth) {
    Writer w(auth);
    w.bytes("NTLMSSP", 8);
    w.u32(3);
    w.zeros(48);
    w.u32((flags & ~(NEGOTIATE_KEY_EXCH | NEGOTIATE_SIGN)) |
          NEGOTIATE_ANONYMOUS);
    writeVersion(w);
    w.zeros(16);  // MIC
    field(w, 12, w.pos(), 1);  // LM response: Z(1)
    w.u8(0);
    for (size_t at : {20, 28, 36, 44, 52}) field(w, at, w.pos(), 0);
  }

  bool buildAuthenticate(const uint8_t* serverChallenge,
                         std::vector<uint8_t> info, std::vector<uint8_t>& auth) {
    // NTOWFv2
    std::vector<uint8_t> tmp;
    Writer t(tmp);
    t.utf16(password);
    uint8_t ntHash[16];
    Crypto::md4(tmp.data(), tmp.size(), ntHash);
    tmp.clear();
    t.utf16(upper(user));
    t.utf16(domain);
    uint8_t responseKey[16];
    Crypto::hmacMd5(ntHash, 16, tmp.data(), tmp.size(), responseKey);

    // timestamp from the server: then we must provide a MIC
    uint64_t timestamp = 0;
    size_t eol = (size_t)-1;
    size_t flagsPos = (size_t)-1;
    Reader r(info.data(), info.size());
    for (size_t off = 0; off + 4 <= info.size();) {
      uint16_t id = r.u16(off);
      uint16_t len = r.u16(off + 2);
      if (id == 0) {
        eol = off;
        break;
      }
      if (id == 7 && len == 8) timestamp = r.u64(off + 4);
      if (id == 6 && len == 4) flagsPos = off + 4;
      off += 4 + len;
    }
    if (eol == (size_t)-1) {
      eol = info.size();
      info.insert(info.end(), 4, 0);
    }
    useMic = timestamp != 0;
    SMB_LOGD("NTLM MIC: %s", useMic ? "yes" : "no");
    if (useMic) {
      Writer iw(info);
      if (flagsPos != (size_t)-1) {
        iw.put32(flagsPos, r.u32(flagsPos) | 0x2);  // MIC provided
      } else {
        const uint8_t avFlags[8] = {6, 0, 4, 0, 2, 0, 0, 0};
        info.insert(info.begin() + eol, avFlags, avFlags + 8);
      }
    } else {
      timestamp = toFileTime(Platform::unixTime());
    }

    uint8_t clientChallenge[8];
    for (auto& b : clientChallenge) b = Platform::randomByte();

    // NTLMv2 response = NTProofStr + temp
    std::vector<uint8_t> temp;
    Writer tw(temp);
    tw.u8(1);
    tw.u8(1);
    tw.zeros(6);
    tw.u64(timestamp);
    tw.bytes(clientChallenge, 8);
    tw.u32(0);
    tw.bytes(info.data(), info.size());
    tw.u32(0);
    tmp.assign(serverChallenge, serverChallenge + 8);
    tmp.insert(tmp.end(), temp.begin(), temp.end());
    uint8_t proof[16];
    Crypto::hmacMd5(responseKey, 16, tmp.data(), tmp.size(), proof);
    std::vector<uint8_t> nt(proof, proof + 16);
    nt.insert(nt.end(), temp.begin(), temp.end());

    // LMv2 response (zeros if a MIC is used)
    std::vector<uint8_t> lm(24, 0);
    if (!useMic) {
      tmp.assign(serverChallenge, serverChallenge + 8);
      tmp.insert(tmp.end(), clientChallenge, clientChallenge + 8);
      Crypto::hmacMd5(responseKey, 16, tmp.data(), tmp.size(), lm.data());
      memcpy(lm.data() + 16, clientChallenge, 8);
    }

    // session key
    uint8_t baseKey[16];
    Crypto::hmacMd5(responseKey, 16, proof, 16, baseKey);
    uint8_t encryptedKey[16];
    bool keyExchange = flags & NEGOTIATE_KEY_EXCH;
    if (keyExchange) {
      for (auto& b : exportedKey) b = Platform::randomByte();
      memcpy(encryptedKey, exportedKey, 16);
      Crypto::rc4(baseKey, 16, encryptedKey, 16);
    } else {
      memcpy(exportedKey, baseKey, 16);
    }

    // AUTHENTICATE message
    Writer w(auth);
    w.bytes("NTLMSSP", 8);
    w.u32(3);
    w.zeros(48);  // fields: patched below
    w.u32(flags);
    writeVersion(w);
    size_t micPos = w.pos();
    w.zeros(16);
    field(w, 12, w.pos(), lm.size());
    w.bytes(lm.data(), lm.size());
    field(w, 20, w.pos(), nt.size());
    w.bytes(nt.data(), nt.size());
    size_t off = w.pos();
    field(w, 28, off, w.utf16(domain));
    off = w.pos();
    field(w, 36, off, w.utf16(user));
    off = w.pos();
    field(w, 44, off, w.utf16("ARDUINO"));
    field(w, 52, w.pos(), keyExchange ? 16 : 0);
    if (keyExchange) w.bytes(encryptedKey, 16);

    if (useMic) {
      tmp = negotiateMsg;
      tmp.insert(tmp.end(), challengeMsg.begin(), challengeMsg.end());
      tmp.insert(tmp.end(), auth.begin(), auth.end());
      Crypto::hmacMd5(exportedKey, 16, tmp.data(), tmp.size(),
                      auth.data() + micPos);
    }
    return true;
  }

  /// NTLM signature (sequence number 0) over the SPNEGO mechTypes
  std::vector<uint8_t> mechListMic() {
    static const char signMagic[] =
        "session key to client-to-server signing key magic constant";
    static const char sealMagic[] =
        "session key to client-to-server sealing key magic constant";
    auto deriveKey = [&](const char* magic, size_t len, uint8_t out[16]) {
      std::vector<uint8_t> d(exportedKey, exportedKey + 16);
      d.insert(d.end(), (const uint8_t*)magic, (const uint8_t*)magic + len);
      Crypto::md5(d.data(), d.size(), out);
    };
    uint8_t signKey[16], sealKey[16];
    deriveKey(signMagic, sizeof(signMagic), signKey);  // including \0
    deriveKey(sealMagic, sizeof(sealMagic), sealKey);

    std::vector<uint8_t> data = {0, 0, 0, 0};  // sequence number
    std::vector<uint8_t> types = mechTypes();
    data.insert(data.end(), types.begin(), types.end());
    uint8_t mac[16];
    Crypto::hmacMd5(signKey, 16, data.data(), data.size(), mac);
    if (flags & NEGOTIATE_KEY_EXCH) Crypto::rc4(sealKey, 16, mac, 8);

    std::vector<uint8_t> sig;
    Writer w(sig);
    w.u32(1);
    w.bytes(mac, 8);
    w.u32(0);
    return sig;
  }
};

}  // namespace smb
