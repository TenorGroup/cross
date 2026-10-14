#pragma once
// The transport under KOReaderSyncClient, answered from a canned reply. Only the calls the client makes.
#include <Arduino.h>

#include <string>

namespace freeink {
class SecureHttpClient {
 public:
  static inline int replyCode = 200;
  static inline std::string replyBody;
  void setCACert(const char*) {}
  void setTimeout(uint32_t) {}
  bool begin(const std::string&) { return true; }
  void addHeader(const char*, const std::string&) {}
  int GET() { return replyCode; }
  int sendRequest(const char*, const std::string&) { return replyCode; }
  const std::string& getString() const { return replyBody; }
  void end() {}
};
}  // namespace freeink
