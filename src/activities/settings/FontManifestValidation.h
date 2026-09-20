#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "../../../freeink-sdk/libs/network/SecureNet/include/HttpUrl.h"

namespace font_manifest {

// FAT names compare case-insensitively. Manifest names use ASCII; keep this
// independent of locale and avoid allocating a folded copy of every path.
inline bool sameAsciiName(const char* a, const char* b) {
  if (!a || !b) return false;
  for (; *a && *b; ++a, ++b) {
    const char ca = *a >= 'A' && *a <= 'Z' ? *a + ('a' - 'A') : *a;
    const char cb = *b >= 'A' && *b <= 'Z' ? *b + ('a' - 'A') : *b;
    if (ca != cb) return false;
  }
  return *a == *b;
}

// These checks run on the parsed DOM before the activity clears its current
// table or reads installation state. Keep them independent of SD and UI code
// so host tests exercise the same production preflight as the downloader.
inline bool validateRequiredShape(JsonVariantConst document) {
  if (!document.is<JsonObjectConst>()) return false;

  const JsonVariantConst familiesValue = document["families"];
  if (!familiesValue.is<JsonArrayConst>()) return false;
  const JsonArrayConst families = familiesValue.as<JsonArrayConst>();
  if (families.isNull() || families.size() == 0) return false;

  uint64_t totalFileEntries = 0;
  size_t familyIndex = 0;
  for (const JsonVariantConst familyValue : families) {
    if (!familyValue.is<JsonObjectConst>()) return false;
    const JsonObjectConst family = familyValue.as<JsonObjectConst>();
    const char* familyName = family["name"].as<const char*>();
    if (!familyName || *familyName == '\0') return false;
    size_t previousFamily = 0;
    for (const JsonVariantConst previous : families) {
      if (previousFamily++ >= familyIndex) break;
      if (sameAsciiName(familyName, previous["name"].as<const char*>())) return false;
    }
    ++familyIndex;

    const JsonVariantConst filesValue = family["files"];
    if (!filesValue.is<JsonArrayConst>()) return false;
    const JsonArrayConst files = filesValue.as<JsonArrayConst>();
    if (files.isNull() || files.size() == 0) return false;

    uint64_t familyTotal = 0;
    size_t fileIndex = 0;
    for (const JsonVariantConst fileValue : files) {
      if (!fileValue.is<JsonObjectConst>()) return false;
      const JsonObjectConst file = fileValue.as<JsonObjectConst>();
      const char* fileName = file["name"].as<const char*>();
      if (!fileName || *fileName == '\0') return false;
      size_t previousFile = 0;
      for (const JsonVariantConst previous : files) {
        if (previousFile++ >= fileIndex) break;
        if (sameAsciiName(fileName, previous["name"].as<const char*>())) return false;
      }
      ++fileIndex;
      if (!file["size"].is<uint32_t>() || file["size"].as<uint32_t>() == 0) return false;
      if (!file["crc32"].is<uint32_t>()) return false;

      familyTotal += file["size"].as<uint32_t>();
      if (familyTotal > std::numeric_limits<uint32_t>::max()) return false;
      if (totalFileEntries >= std::numeric_limits<uint32_t>::max()) return false;
      ++totalFileEntries;
    }
  }

  return true;
}

// A manifest base URL is a URL prefix because downloadFamily appends the
// filename directly. Both HTTP and HTTPS remain supported, matching the
// existing downloader policy; query and fragment components are rejected so
// an appended filename cannot be swallowed by either component.
inline bool isSupportedBaseUrl(const char* value) {
  if (!value || *value == '\0') return false;
  const std::string_view url(value);
  if (url.find_first_of("?#") != std::string_view::npos) return false;

  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(url, parsed)) return false;
  return !parsed.target.empty() && parsed.target.back() == '/';
}

}  // namespace font_manifest
