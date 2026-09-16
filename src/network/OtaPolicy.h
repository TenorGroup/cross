#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ota_policy {
struct Version {
  uint32_t parts[3] = {};
  bool prerelease = false;
};
inline bool parseVersion(const char* text, Version& out, bool stableOnly = false) {
  out = {};
  if (!text) return false;
  if (*text == 'v') ++text;
  for (int part = 0; part < 3; ++part) {
    const char* start = text;
    if (*text < '0' || *text > '9') return false;
    while (*text >= '0' && *text <= '9') {
      if (out.parts[part] > 9999) return false;
      out.parts[part] = out.parts[part] * 10 + (*text++ - '0');
    }
    if (text - start > 1 && *start == '0') return false;
    if (part < 2 && *text++ != '.') return false;
  }
  if (!*text) return true;
  if (stableOnly || (*text != '-' && *text != '+')) return false;
  out.prerelease = *text == '-';
  ++text;
  if (!*text) return false;
  for (; *text; ++text) {
    if (!((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
          *text == '.' || *text == '-' || *text == '+'))
      return false;
  }
  return true;
}
inline bool stableIsNewer(const char* current, const char* offered) {
  Version have, next;
  if (!parseVersion(current, have) || !parseVersion(offered, next, true)) return false;
  for (int part = 0; part < 3; ++part) {
    if (have.parts[part] != next.parts[part]) return next.parts[part] > have.parts[part];
  }
  return have.prerelease;
}
inline bool firmwareUrlAllowed(const char* url) {
  constexpr char prefix[] = "https://cross.tenor.vn/firmware/";
  if (!url || std::strncmp(url, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* path = url + sizeof(prefix) - 1;
  if (!*path || std::strstr(path, "..")) return false;
  for (const char* p = path; *p; ++p) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
          *p == '.' || *p == '/'))
      return false;
  }
  const size_t n = std::strlen(path);
  return n > 4 && std::strcmp(path + n - 4, ".bin") == 0;
}
inline bool decodeDigest(const char* text, uint8_t* out) {
  if (!text || std::strncmp(text, "sha256:", 7) || std::strlen(text) != 71) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char c = text[i + 7];
    int v = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                   : -1;
    if (v < 0) return false;
    if ((i & 1) == 0)
      out[i / 2] = static_cast<uint8_t>(v << 4);
    else
      out[i / 2] |= static_cast<uint8_t>(v);
  }
  return true;
}
}  // namespace ota_policy
