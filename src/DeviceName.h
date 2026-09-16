#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"

// One place that answers "what is this device called on the network".
// The Wi-Fi hostname, the mDNS name and the access-point SSID all ask here, so
// a user who renames the device sees the new name everywhere at once.
//
// The user's name is sanitised to an RFC 1123 host label: letters, digits and
// hyphens only, no leading or trailing hyphen, at most 63 characters. Anything
// else collapses to a hyphen. An empty result falls back to the caller's
// default, so a blank setting keeps the stock behaviour.
inline void deviceNetworkName(char* out, const size_t size, const char* fallback) {
  if (out == nullptr || size == 0) {
    return;
  }

  size_t written = 0;
  bool lastWasHyphen = true;  // suppresses a leading hyphen
  for (const char* p = SETTINGS.deviceName; *p != '\0' && written + 1 < size; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (alnum) {
      out[written++] = static_cast<char>(c);
      lastWasHyphen = false;
    } else if (!lastWasHyphen) {
      out[written++] = '-';
      lastWasHyphen = true;
    }
  }
  while (written > 0 && out[written - 1] == '-') {
    --written;  // drop a trailing hyphen
  }
  out[written] = '\0';

  if (written == 0 && fallback != nullptr) {
    snprintf(out, size, "%s", fallback);
  }
}
