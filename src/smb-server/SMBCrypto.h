#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace smb {

/**
 * @brief Minimal, dependency free implementations of the hash and cipher
 * primitives which are needed by NTLMv2 authentication and SMB 2.x message
 * signing: MD4, MD5, HMAC-MD5, RC4, SHA-256 and HMAC-SHA256.
 */
class Crypto {
 public:
  static inline uint32_t rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
  }
  static inline uint32_t ror(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  // ---------------------------------------------------------------- MD4/MD5
  /// Shared Merkle-Damgard padding for MD4/MD5 (little endian length)
  template <typename F>
  static void mdPad(const uint8_t* data, size_t len, F block) {
    size_t i = 0;
    for (; i + 64 <= len; i += 64) block(data + i);
    uint8_t buf[128] = {0};
    size_t rest = len - i;
    memcpy(buf, data + i, rest);
    buf[rest] = 0x80;
    size_t total = (rest + 9 <= 64) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 0; j < 8; j++) buf[total - 8 + j] = (uint8_t)(bits >> (8 * j));
    block(buf);
    if (total == 128) block(buf + 64);
  }

  static void md4(const uint8_t* data, size_t len, uint8_t out[16]) {
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    mdPad(data, len, [&](const uint8_t* p) {
      uint32_t x[16];
      for (int i = 0; i < 16; i++) x[i] = le32(p + 4 * i);
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
      auto f = [](uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) | (~x & z);
      };
      auto g = [](uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) | (x & z) | (y & z);
      };
      auto h3 = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; };
      static const int s1[4] = {3, 7, 11, 19}, s2[4] = {3, 5, 9, 13},
                       s3[4] = {3, 9, 11, 15};
      static const int o3[16] = {0, 8, 4, 12, 2, 10, 6, 14,
                                 1, 9, 5, 13, 3, 11, 7, 15};
      for (int i = 0; i < 16; i++) {
        uint32_t t = rol(a + f(b, c, d) + x[i], s1[i % 4]);
        a = d; d = c; c = b; b = t;
      }
      for (int i = 0; i < 16; i++) {
        int k = (i % 4) * 4 + i / 4;
        uint32_t t = rol(a + g(b, c, d) + x[k] + 0x5a827999, s2[i % 4]);
        a = d; d = c; c = b; b = t;
      }
      for (int i = 0; i < 16; i++) {
        uint32_t t = rol(a + h3(b, c, d) + x[o3[i]] + 0x6ed9eba1, s3[i % 4]);
        a = d; d = c; c = b; b = t;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    });
    for (int i = 0; i < 4; i++) put32(out + 4 * i, h[i]);
  }

  /// Incremental MD5 is not needed: HMAC is computed on concatenated buffers
  static void md5(const uint8_t* data, size_t len, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint8_t S[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17,
                                  22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                                  14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,
                                  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                                  4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15,
                                  21, 6,  10, 15, 21, 6,  10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    mdPad(data, len, [&](const uint8_t* p) {
      uint32_t m[16];
      for (int i = 0; i < 16; i++) m[i] = le32(p + 4 * i);
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
      for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
          f = (b & c) | (~b & d);
          g = i;
        } else if (i < 32) {
          f = (d & b) | (~d & c);
          g = (5 * i + 1) % 16;
        } else if (i < 48) {
          f = b ^ c ^ d;
          g = (3 * i + 5) % 16;
        } else {
          f = c ^ (b | ~d);
          g = (7 * i) % 16;
        }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rol(a + f + K[i] + m[g], S[i]);
        a = t;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    });
    for (int i = 0; i < 4; i++) put32(out + 4 * i, h[i]);
  }

  // ---------------------------------------------------------------- SHA-256
  /// Incremental SHA-256 so that large SMB messages can be hashed in place
  class Sha256 {
   public:
    Sha256() { reset(); }
    void reset() {
      static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                       0xa54ff53a, 0x510e527f, 0x9b05688c,
                                       0x1f83d9ab, 0x5be0cd19};
      memcpy(h, init, sizeof(h));
      total = 0;
      used = 0;
    }
    void update(const uint8_t* data, size_t len) {
      total += len;
      while (len > 0) {
        size_t n = 64 - used;
        if (n > len) n = len;
        memcpy(buf + used, data, n);
        used += n;
        data += n;
        len -= n;
        if (used == 64) {
          block(buf);
          used = 0;
        }
      }
    }
    void finish(uint8_t out[32]) {
      uint64_t bits = total * 8;
      uint8_t pad = 0x80;
      update(&pad, 1);
      uint8_t zero = 0;
      while (used != 56) update(&zero, 1);
      uint8_t len[8];
      for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
      update(len, 8);
      for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
      }
    }

   protected:
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t total;
    size_t used;

    void block(const uint8_t* p) {
      static const uint32_t K[64] = {
          0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
          0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
          0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
          0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
          0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
          0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
          0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
          0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
          0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
          0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
          0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
          0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
          0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
      uint32_t w[64];
      for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
      for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
               g = h[6], hh = h[7];
      for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d;
      h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
  };

  /// HMAC-SHA256 over the concatenation of up to two buffers
  static void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* d1,
                         size_t l1, const uint8_t* d2, size_t l2,
                         uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (keyLen > 64) {
      Sha256 s;
      s.update(key, keyLen);
      s.finish(k);
    } else {
      memcpy(k, key, keyLen);
    }
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    Sha256 inner;
    inner.update(pad, 64);
    if (l1) inner.update(d1, l1);
    if (l2) inner.update(d2, l2);
    uint8_t ih[32];
    inner.finish(ih);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    Sha256 outer;
    outer.update(pad, 64);
    outer.update(ih, 32);
    outer.finish(out);
  }

  /// HMAC-MD5 (key <= 64 bytes, which is always the case for NTLM)
  static void hmacMd5(const uint8_t* key, size_t keyLen, const uint8_t* data,
                      size_t len, uint8_t out[16]) {
    uint8_t k[64] = {0};
    if (keyLen > 64) {
      md5(key, keyLen, k);
    } else {
      memcpy(k, key, keyLen);
    }
    uint8_t* tmp = new uint8_t[64 + (len > 16 ? len : 16)];
    for (int i = 0; i < 64; i++) tmp[i] = k[i] ^ 0x36;
    memcpy(tmp + 64, data, len);
    uint8_t ih[16];
    md5(tmp, 64 + len, ih);
    for (int i = 0; i < 64; i++) tmp[i] = k[i] ^ 0x5c;
    memcpy(tmp + 64, ih, 16);
    md5(tmp, 80, out);
    delete[] tmp;
  }

  // -------------------------------------------------------------------- RC4
  static void rc4(const uint8_t* key, size_t keyLen, uint8_t* data,
                  size_t len) {
    uint8_t s[256];
    for (int i = 0; i < 256; i++) s[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
      j = j + s[i] + key[i % keyLen];
      uint8_t t = s[i];
      s[i] = s[j];
      s[j] = t;
    }
    uint8_t a = 0;
    j = 0;
    for (size_t n = 0; n < len; n++) {
      a++;
      j += s[a];
      uint8_t t = s[a];
      s[a] = s[j];
      s[j] = t;
      data[n] ^= s[(uint8_t)(s[a] + s[j])];
    }
  }

  /// Constant time comparison
  static bool equals(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t r = 0;
    for (size_t i = 0; i < len; i++) r |= a[i] ^ b[i];
    return r == 0;
  }

 protected:
  static inline uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
  }
  static inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
  }
};

}  // namespace smb
