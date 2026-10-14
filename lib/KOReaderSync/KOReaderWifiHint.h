#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

// The channel and access point of the network the automatic sync last joined, so the next join can skip
// the scan. A hint only counts for the network it was taken from and only while it is well formed; a
// hint that fails to connect is dropped by the caller and the join goes the normal way. Stored in
// /.crosspoint/kosync-wifi.json; the Wi-Fi list and the saved networks are not touched.
struct WifiHint {
  std::string ssid;
  uint8_t channel = 0;
  uint8_t bssid[6] = {};

  // Well formed: a network name, a 2.4 GHz channel 1-14, and a unicast access point address.
  bool wellFormed() const;
  // Usable for this saved network.
  bool usableFor(const std::string& wantedSsid) const { return wellFormed() && ssid == wantedSsid; }

  void toJson(JsonDocument& doc) const;
  // false (and an empty hint) when the document does not hold a well formed hint.
  bool fromJson(JsonVariantConst doc);
  bool operator==(const WifiHint& other) const;
};
