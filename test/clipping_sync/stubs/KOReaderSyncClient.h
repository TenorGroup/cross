#pragma once
#include <string>
#include <vector>

#include "SecureHttpClient.h"

// The KOReader client's connection opening, which clipping sync must use (D9). The real one parses the URL, waits
// for a real clock, sets the roots for the host and begins; here it records the URLs it was asked to open.
namespace fake {
inline std::vector<std::string> opened;
inline bool refuseOpen = false;
}  // namespace fake
struct KOReaderSyncClient {
  static bool openConnection(freeink::SecureHttpClient& http, const std::string& url) {
    fake::opened.push_back(url);
    return !fake::refuseOpen && http.begin(url);
  }
};
