#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

namespace smb {

/**
 * @brief Little endian reader on top of a byte range. Out of range reads
 * return 0 and set the error flag so that malformed requests can be detected
 * without exceptions.
 */
class Reader {
 public:
  Reader(const uint8_t* data = nullptr, size_t len = 0)
      : data(data), len(len) {}

  uint8_t u8(size_t off) const { return off < len ? data[off] : (err(), 0); }
  uint16_t u16(size_t off) const {
    if (off + 2 > len) return err(), 0;
    return (uint16_t)(data[off] | (data[off + 1] << 8));
  }
  uint32_t u32(size_t off) const {
    if (off + 4 > len) return err(), 0;
    return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
  }
  uint64_t u64(size_t off) const {
    return (uint64_t)u32(off) | ((uint64_t)u32(off + 4) << 32);
  }
  /// Returns a pointer to len bytes at off or nullptr if out of range
  const uint8_t* ptr(size_t off, size_t n) const {
    if (off > len || n > len - off) return err(), nullptr;
    return data + off;
  }
  /// Decodes a UTF-16LE string into UTF-8
  std::string utf16(size_t off, size_t bytes) const {
    const uint8_t* p = ptr(off, bytes);
    if (p == nullptr) return std::string();
    return fromUtf16(p, bytes);
  }

  static std::string fromUtf16(const uint8_t* p, size_t bytes) {
    std::string out;
    for (size_t i = 0; i + 1 < bytes; i += 2) {
      uint32_t c = p[i] | (p[i + 1] << 8);
      if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
        uint32_t c2 = p[i + 2] | (p[i + 3] << 8);
        if (c2 >= 0xDC00 && c2 < 0xE000) {
          c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
          i += 2;
        }
      }
      appendUtf8(out, c);
    }
    return out;
  }

  static void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
      out += (char)c;
    } else if (c < 0x800) {
      out += (char)(0xC0 | (c >> 6));
      out += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += (char)(0xE0 | (c >> 12));
      out += (char)(0x80 | ((c >> 6) & 0x3F));
      out += (char)(0x80 | (c & 0x3F));
    } else {
      out += (char)(0xF0 | (c >> 18));
      out += (char)(0x80 | ((c >> 12) & 0x3F));
      out += (char)(0x80 | ((c >> 6) & 0x3F));
      out += (char)(0x80 | (c & 0x3F));
    }
  }

  size_t size() const { return len; }
  bool hasError() const { return error; }

 protected:
  const uint8_t* data;
  size_t len;
  mutable bool error = false;
  void err() const { error = true; }
};

/**
 * @brief Little endian writer which appends to (or patches) a std::vector
 */
class Writer {
 public:
  Writer(std::vector<uint8_t>& buf) : buf(buf) {}

  size_t pos() const { return buf.size(); }
  void u8(uint8_t v) { buf.push_back(v); }
  void u16(uint16_t v) {
    u8((uint8_t)v);
    u8((uint8_t)(v >> 8));
  }
  void u32(uint32_t v) {
    u16((uint16_t)v);
    u16((uint16_t)(v >> 16));
  }
  void u64(uint64_t v) {
    u32((uint32_t)v);
    u32((uint32_t)(v >> 32));
  }
  void bytes(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    buf.insert(buf.end(), b, b + n);
  }
  void zeros(size_t n) { buf.insert(buf.end(), n, 0); }
  /// Pads with zeros so that (pos() - base) is a multiple of align
  void align(size_t align, size_t base = 0) {
    while ((pos() - base) % align) u8(0);
  }
  /// Encodes a UTF-8 string as UTF-16LE; returns the number of bytes written
  size_t utf16(const std::string& s, bool nullTerminate = false) {
    size_t start = pos();
    for (size_t i = 0; i < s.size();) {
      uint32_t c = (uint8_t)s[i];
      int extra = 0;
      if (c >= 0xF0) {
        c &= 0x07;
        extra = 3;
      } else if (c >= 0xE0) {
        c &= 0x0F;
        extra = 2;
      } else if (c >= 0xC0) {
        c &= 0x1F;
        extra = 1;
      }
      i++;
      for (int j = 0; j < extra && i < s.size(); j++, i++)
        c = (c << 6) | ((uint8_t)s[i] & 0x3F);
      if (c >= 0x10000) {
        c -= 0x10000;
        u16((uint16_t)(0xD800 + (c >> 10)));
        u16((uint16_t)(0xDC00 + (c & 0x3FF)));
      } else {
        u16((uint16_t)c);
      }
    }
    if (nullTerminate) u16(0);
    return pos() - start;
  }

  void put16(size_t at, uint16_t v) {
    buf[at] = (uint8_t)v;
    buf[at + 1] = (uint8_t)(v >> 8);
  }
  void put32(size_t at, uint32_t v) {
    put16(at, (uint16_t)v);
    put16(at + 2, (uint16_t)(v >> 16));
  }
  void put64(size_t at, uint64_t v) {
    put32(at, (uint32_t)v);
    put32(at + 4, (uint32_t)(v >> 32));
  }

  std::vector<uint8_t>& buffer() { return buf; }

 protected:
  std::vector<uint8_t>& buf;
};

}  // namespace smb
