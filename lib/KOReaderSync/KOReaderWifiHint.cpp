#include "KOReaderWifiHint.h"

#include <cctype>
#include <cstdio>
#include <cstring>

bool WifiHint::wellFormed() const {
  if (ssid.empty() || ssid.size() > 32) return false;
  if (channel < 1 || channel > 14) return false;
  bool allZero = true;
  bool allOnes = true;
  for (const uint8_t b : bssid) {
    allZero = allZero && b == 0;
    allOnes = allOnes && b == 0xFF;
  }
  if (allZero || allOnes) return false;
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  if (fastJoinFailures > 2) return false;
#endif
  return (bssid[0] & 0x01) == 0;  // the multicast bit is never an access point
}

void WifiHint::toJson(JsonDocument& doc) const {
  char hex[13];
  snprintf(hex, sizeof(hex), "%02x%02x%02x%02x%02x%02x", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  doc["ssid"] = ssid;
  doc["ch"] = channel;
  doc["bssid"] = hex;
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  if (fastJoinFailures) doc["fast_join_failures"] = fastJoinFailures;
#endif
}

bool WifiHint::fromJson(JsonVariantConst doc) {
  *this = WifiHint{};
  const char* name = doc["ssid"].as<const char*>();
  const char* hex = doc["bssid"].as<const char*>();
  const uint32_t ch = doc["ch"] | 0u;
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  const uint32_t failures = doc["fast_join_failures"] | 0u;
  if (!name || !hex || ch > 255 || failures > 2 || strlen(hex) != 12) return false;
#else
  if (!name || !hex || ch > 255 || strlen(hex) != 12) return false;
#endif
  WifiHint parsed;
  parsed.ssid = name;
  parsed.channel = static_cast<uint8_t>(ch);
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  parsed.fastJoinFailures = static_cast<uint8_t>(failures);
#endif
  for (int i = 0; i < 6; ++i) {
    const char pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    if (!isxdigit(static_cast<unsigned char>(pair[0])) || !isxdigit(static_cast<unsigned char>(pair[1]))) return false;
    parsed.bssid[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
  }
  if (!parsed.wellFormed()) return false;
  *this = parsed;
  return true;
}

bool WifiHint::operator==(const WifiHint& other) const {
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  return ssid == other.ssid && channel == other.channel && fastJoinFailures == other.fastJoinFailures &&
         memcmp(bssid, other.bssid, sizeof(bssid)) == 0;
#else
  return ssid == other.ssid && channel == other.channel && memcmp(bssid, other.bssid, sizeof(bssid)) == 0;
#endif
}
