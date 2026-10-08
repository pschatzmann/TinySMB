#pragma once
#include <ctype.h>

#include <string>
#include <vector>

#include "SMBBuffer.h"
#include "SMBCrypto.h"
#include "SMBPlatform.h"

namespace smb {

/// User credentials
struct User {
  std::string name;
  std::string password;
  bool readOnly = false;  ///< user can only read: no create, update or delete
};

/// Result of the processing of a session setup security blob
enum class AuthResult { Continue, Success, Guest, Failed };

/**
 * @brief Server side NTLMSSP (NTLMv2) authentication wrapped in SPNEGO as used
 * by SMB2 SESSION_SETUP. Raw NTLMSSP blobs (without SPNEGO) are supported as
 * well.
 */
class NtlmAuth {
 public:
  // NTLMSSP negotiate flags
  static const uint32_t NEGOTIATE_UNICODE = 0x00000001;
  static const uint32_t REQUEST_TARGET = 0x00000004;
  static const uint32_t NEGOTIATE_SIGN = 0x00000010;
  static const uint32_t NEGOTIATE_SEAL = 0x00000020;
  static const uint32_t NEGOTIATE_NTLM = 0x00000200;
  static const uint32_t NEGOTIATE_ANONYMOUS = 0x00000800;
  static const uint32_t NEGOTIATE_ALWAYS_SIGN = 0x00008000;
  static const uint32_t TARGET_TYPE_SERVER = 0x00020000;
  static const uint32_t NEGOTIATE_EXTENDED_SESSIONSECURITY = 0x00080000;
  static const uint32_t NEGOTIATE_TARGET_INFO = 0x00800000;
  static const uint32_t NEGOTIATE_VERSION = 0x02000000;
  static const uint32_t NEGOTIATE_128 = 0x20000000;
  static const uint32_t NEGOTIATE_KEY_EXCH = 0x40000000;
  static const uint32_t NEGOTIATE_56 = 0x80000000;

  /// Builds the SPNEGO NegTokenInit which is returned in the NEGOTIATE
  /// response to announce that we support NTLMSSP
  static std::vector<uint8_t> negotiateToken() {
    std::vector<uint8_t> mechList = der(0x30, ntlmOid());
    std::vector<uint8_t> seq = der(0x30, der(0xA0, mechList));
    std::vector<uint8_t> inner = spnegoOid();
    std::vector<uint8_t> init = der(0xA0, seq);
    inner.insert(inner.end(), init.begin(), init.end());
    return der(0x60, inner);
  }

  /// Defines the names which are reported in the NTLM challenge
  void setNames(const std::string& server, const std::string& domain) {
    serverName = server;
    domainName = domain;
  }

  /**
   * Processes the security blob of a SESSION_SETUP request. On Continue
   * the response blob must be returned with STATUS_MORE_PROCESSING_REQUIRED.
   */
  AuthResult process(const uint8_t* blob, size_t len,
                     const std::vector<User>& users, bool allowGuest,
                     std::vector<uint8_t>& response) {
    response.clear();
    size_t pos = findNtlmssp(blob, len);
    if (pos == (size_t)-1) return AuthResult::Failed;
    if (pos > 0) useSpnego = true;
    Reader msg(blob + pos, len - pos);
    uint32_t type = msg.u32(8);
    if (type == 1) {
      clientFlags = msg.u32(12);
      std::vector<uint8_t> challenge = buildChallenge();
      response = useSpnego ? spnegoResponse(1, &challenge) : challenge;
      return AuthResult::Continue;
    }
    if (type != 3) return AuthResult::Failed;
    AuthResult result = verify(msg, users, allowGuest);
    if (result != AuthResult::Failed && useSpnego) {
      response = spnegoResponse(0, nullptr);
    }
    return result;
  }

  /// Name of the authenticated user
  const std::string& userName() const { return user; }
  /// True if the authenticated user only has read access
  bool isReadOnly() const { return readOnlyUser; }
  /// Exported session key (valid after a successful non guest login)
  const uint8_t* sessionKey() const { return exportedKey; }

 protected:
  bool useSpnego = false;
  uint32_t clientFlags = 0;
  uint32_t flags = 0;
  uint8_t serverChallenge[8] = {0};
  uint8_t exportedKey[16] = {0};
  std::string serverName = "ARDUINO";
  std::string domainName = "WORKGROUP";
  std::string user;
  bool readOnlyUser = false;

  static size_t findNtlmssp(const uint8_t* p, size_t len) {
    static const char sig[] = "NTLMSSP";
    for (size_t i = 0; i + 12 <= len; i++) {
      if (memcmp(p + i, sig, 8) == 0) return i;
    }
    return (size_t)-1;
  }

  /// DER tag-length-value encoding
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

  /// SPNEGO NegTokenResp: state 0 = accept-completed, 1 = accept-incomplete
  static std::vector<uint8_t> spnegoResponse(uint8_t state,
                                             const std::vector<uint8_t>* tok) {
    std::vector<uint8_t> seq = der(0xA0, {0x0A, 0x01, state});
    if (tok != nullptr) {
      std::vector<uint8_t> mech = der(0xA1, ntlmOid());
      std::vector<uint8_t> token = der(0xA2, der(0x04, *tok));
      seq.insert(seq.end(), mech.begin(), mech.end());
      seq.insert(seq.end(), token.begin(), token.end());
    }
    return der(0xA1, der(0x30, seq));
  }

  static void avPair(Writer<>& w, uint16_t id, const std::string& value) {
    w.u16(id);
    size_t lenPos = w.pos();
    w.u16(0);
    size_t n = w.utf16(value);
    w.put16(lenPos, (uint16_t)n);
  }

  std::vector<uint8_t> buildChallenge() {
    for (int i = 0; i < 8; i++) serverChallenge[i] = randomByte();
    flags = NEGOTIATE_UNICODE | REQUEST_TARGET | NEGOTIATE_NTLM |
            NEGOTIATE_ALWAYS_SIGN | TARGET_TYPE_SERVER |
            NEGOTIATE_EXTENDED_SESSIONSECURITY | NEGOTIATE_TARGET_INFO |
            NEGOTIATE_VERSION | NEGOTIATE_128 | NEGOTIATE_56;
    flags |= clientFlags & (NEGOTIATE_SIGN | NEGOTIATE_SEAL |
                            NEGOTIATE_KEY_EXCH | NEGOTIATE_ANONYMOUS);

    std::vector<uint8_t> out;
    Writer w(out);
    w.bytes("NTLMSSP", 8);
    w.u32(2);
    w.zeros(8);  // target name fields: patched below
    w.u32(flags);
    w.bytes(serverChallenge, 8);
    w.zeros(8);
    w.zeros(8);  // target info fields: patched below
    // version: 6.1 build 7601, NTLM revision 15
    const uint8_t version[8] = {6, 1, 0xB1, 0x1D, 0, 0, 0, 15};
    w.bytes(version, 8);

    size_t nameOff = w.pos();
    size_t nameLen = w.utf16(serverName);
    w.put16(12, (uint16_t)nameLen);
    w.put16(14, (uint16_t)nameLen);
    w.put32(16, (uint32_t)nameOff);

    size_t infoOff = w.pos();
    avPair(w, 2, domainName);  // MsvAvNbDomainName
    avPair(w, 1, serverName);  // MsvAvNbComputerName
    avPair(w, 4, domainName);  // MsvAvDnsDomainName
    avPair(w, 3, serverName);  // MsvAvDnsComputerName
    w.u32(0);                  // MsvAvEOL
    size_t infoLen = w.pos() - infoOff;
    w.put16(40, (uint16_t)infoLen);
    w.put16(42, (uint16_t)infoLen);
    w.put32(44, (uint32_t)infoOff);
    return out;
  }

  static std::string upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)toupper((unsigned char)c);
    return r;
  }

  AuthResult verify(const Reader& msg, const std::vector<User>& users,
                    bool allowGuest) {
    auto field = [&](size_t off, size_t& o, size_t& l) {
      l = msg.u16(off);
      o = msg.u32(off + 4);
      return msg.ptr(o, l);
    };
    size_t ntOff, ntLen, domOff, domLen, userOff, userLen, keyOff, keyLen;
    const uint8_t* nt = field(20, ntOff, ntLen);
    field(28, domOff, domLen);
    field(36, userOff, userLen);
    const uint8_t* encKey = field(52, keyOff, keyLen);
    uint32_t authFlags = msg.u32(60);
    if (msg.hasError()) return AuthResult::Failed;

    user = msg.utf16(userOff, userLen);
    std::string domain = msg.utf16(domOff, domLen);

    // anonymous login
    if (user.empty() && ntLen == 0) {
      return allowGuest ? AuthResult::Guest : AuthResult::Failed;
    }

    const User* account = nullptr;
    for (auto& u : users) {
      if (upper(u.name) == upper(user)) account = &u;
    }
    if (account == nullptr) {
      return allowGuest ? AuthResult::Guest : AuthResult::Failed;
    }
    // we only support NTLMv2 (response = 16 byte proof + blob)
    if (nt == nullptr || ntLen <= 24) return AuthResult::Failed;

    // NTOWFv2 = HMAC_MD5(MD4(UNICODE(password)), UNICODE(UPPER(user) + dom))
    std::vector<uint8_t> tmp;
    Writer w(tmp);
    w.utf16(account->password);
    uint8_t ntHash[16];
    Crypto::md4(tmp.data(), tmp.size(), ntHash);
    tmp.clear();
    w.utf16(upper(user));
    w.utf16(domain);
    uint8_t responseKey[16];
    Crypto::hmacMd5(ntHash, 16, tmp.data(), tmp.size(), responseKey);

    // NTProofStr = HMAC_MD5(NTOWFv2, ServerChallenge + blob)
    tmp.clear();
    w.bytes(serverChallenge, 8);
    w.bytes(nt + 16, ntLen - 16);
    uint8_t proof[16];
    Crypto::hmacMd5(responseKey, 16, tmp.data(), tmp.size(), proof);
    if (!Crypto::equals(proof, nt, 16)) return AuthResult::Failed;

    // session key
    uint8_t baseKey[16];
    Crypto::hmacMd5(responseKey, 16, proof, 16, baseKey);
    memcpy(exportedKey, baseKey, 16);
    if ((authFlags & NEGOTIATE_KEY_EXCH) && encKey != nullptr &&
        keyLen == 16) {
      memcpy(exportedKey, encKey, 16);
      Crypto::rc4(baseKey, 16, exportedKey, 16);
    }
    user = account->name;
    readOnlyUser = account->readOnly;
    return AuthResult::Success;
  }

  /// DER encoded SPNEGO OID 1.3.6.1.5.5.2
  static std::vector<uint8_t> spnegoOid() {
    return {0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
  }
  /// DER encoded NTLMSSP OID 1.3.6.1.4.1.311.2.2.10
  static std::vector<uint8_t> ntlmOid() {
    return {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
  }
  static uint8_t randomByte() { return Platform::randomByte(); }
};

}  // namespace smb
