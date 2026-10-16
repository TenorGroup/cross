#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

// The channel and access point of the network the automatic sync last joined, so the next join can skip
// the scan. A hint only counts for the network it was taken from and only while it is well formed. A hint
// that fails to connect is counted by the caller, and later rounds use the normal scan. Stored in
// /.crosspoint/kosync-wifi.json; the Wi-Fi list and the saved networks are not touched.
struct WifiHint {
  std::string ssid;
  uint8_t channel = 0;
  uint8_t bssid[6] = {};
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
  // A failed fast join stays associated with this exact hint. The next round scans directly,
  // so a stale access point cannot cost another FAST_JOIN_MS before the normal join.
  uint8_t fastJoinFailures = 0;
#endif

  // Well formed: a network name, a 2.4 GHz channel 1-14, and a unicast access point address.
  bool wellFormed() const;
  // Usable for this saved network.
  bool usableFor(const std::string& wantedSsid) const { return wellFormed() && ssid == wantedSsid; }

  void toJson(JsonDocument& doc) const;
  // false (and an empty hint) when the document does not hold a well formed hint.
  bool fromJson(JsonVariantConst doc);
  bool operator==(const WifiHint& other) const;
};

#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
// A failed hint marks the next round to skip the measured 4 s dead wait. The counter remains
// saturating so damaged or old values cannot wrap into a fast attempt.
constexpr bool shouldTryFastJoin(const uint8_t consecutiveFailures) { return consecutiveFailures == 0; }
constexpr uint8_t recordFastJoinFailure(const uint8_t consecutiveFailures) {
  return consecutiveFailures < 2 ? static_cast<uint8_t>(consecutiveFailures + 1) : 2;
}
#endif
