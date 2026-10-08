// Unit tests for the crypto primitives and helpers (known test vectors)
#include <stdio.h>

#include "SMB.h"

static int failures = 0;

static std::string hex(const uint8_t* p, size_t n) {
  std::string s;
  char b[3];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02x", p[i]);
    s += b;
  }
  return s;
}

static void check(const char* name, const std::string& actual,
                  const char* expected) {
  bool ok = actual == expected;
  if (!ok) failures++;
  printf("%-14s %s %s\n", name, ok ? "ok  " : "FAIL", actual.c_str());
}

int main() {
  const uint8_t* abc = (const uint8_t*)"abc";
  const char* longText =
      "1234567890123456789012345678901234567890123456789012345678901234567890"
      "1234567890";
  uint8_t out[32];

  Crypto::md4(abc, 3, out);
  check("md4", hex(out, 16), "a448017aaf21d8525fc10ae87aa6729d");
  Crypto::md4((const uint8_t*)longText, strlen(longText), out);
  check("md4-long", hex(out, 16), "e33b4ddc9c38f2199c3e7b164fcc0536");
  Crypto::md5(abc, 3, out);
  check("md5", hex(out, 16), "900150983cd24fb0d6963f7d28e17f72");
  Crypto::md5((const uint8_t*)longText, strlen(longText), out);
  check("md5-long", hex(out, 16), "57edf4a22be3c955ac49da2e2107b67a");

  Crypto::Sha256 sha;
  sha.update(abc, 3);
  sha.finish(out);
  check("sha256", hex(out, 32),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // RFC 2202 / RFC 4231 test case 2
  const uint8_t* key = (const uint8_t*)"Jefe";
  const char* data = "what do ya want for nothing?";
  Crypto::hmacMd5(key, 4, (const uint8_t*)data, strlen(data), out);
  check("hmac-md5", hex(out, 16), "750c783e6ab0b503eaa86e310a5db738");
  Crypto::hmacSha256(key, 4, (const uint8_t*)data, 10,
                     (const uint8_t*)data + 10, strlen(data) - 10, out);
  check("hmac-sha256", hex(out, 32),
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

  uint8_t text[9] = {'P', 'l', 'a', 'i', 'n', 't', 'e', 'x', 't'};
  Crypto::rc4((const uint8_t*)"Key", 3, text, 9);
  check("rc4", hex(text, 9), "bbf316e8d940af0ad3");

  // MS-NLMP 4.2.4.1.1: NTOWFv2("Password", "User", "Domain")
  std::vector<uint8_t> tmp;
  Writer w(tmp);
  w.utf16("Password");
  uint8_t ntHash[16];
  Crypto::md4(tmp.data(), tmp.size(), ntHash);
  tmp.clear();
  w.utf16("USER");
  w.utf16("Domain");
  Crypto::hmacMd5(ntHash, 16, tmp.data(), tmp.size(), out);
  check("ntowfv2", hex(out, 16), "0c868a403bfd7a93a3001ef22ef02e3f");

  tmp.clear();
  w.utf16("Grüße \xF0\x9F\x98\x80");
  check("utf16", Reader::fromUtf16(tmp.data(), tmp.size()),
        "Grüße \xF0\x9F\x98\x80");

  printf("%s\n", failures ? "FAILED" : "ALL TESTS PASSED");
  return failures ? 1 : 0;
}
