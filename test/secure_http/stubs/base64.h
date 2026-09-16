#pragma once
#include <string>
namespace base64 {
inline std::string encode(const char* text) {
  constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  unsigned value = 0;
  int bits = -6;
  for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
    value = (value << 8) | *p;
    bits += 8;
    while (bits >= 0) {
      out += table[(value >> bits) & 63];
      bits -= 6;
    }
  }
  if (bits > -6) out += table[((value << 8) >> (bits + 8)) & 63];
  while (out.size() % 4) out += '=';
  return out;
}
}  // namespace base64
