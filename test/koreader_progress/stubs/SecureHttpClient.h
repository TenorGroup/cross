#pragma once
// The transport under KOReaderSyncClient, answered from a canned reply. Only the calls the client makes.
#include <Arduino.h>

#include <algorithm>
#include <functional>
#include <string>

namespace freeink {
class SecureHttpClient {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  using AbortCallback = std::function<bool()>;

  static inline int replyCode = 200;
  static inline std::string replyBody;
  static inline size_t replyChunkBytes = 0;
  static inline uint32_t advanceMsPerChunk = 0;
  static inline uint32_t nowMs = 0;
  static inline bool usedStreamingGet = false;
  void setCACert(const char*) {}
  void setTimeout(uint32_t) {}
  bool begin(const std::string&) { return true; }
  void addHeader(const std::string&, const std::string&) {}
  int GET() {
    body_ = replyBody;
    complete_ = true;
    return replyCode;
  }
  int GET(const DataCallback& onData, const AbortCallback& shouldAbort = nullptr) {
    usedStreamingGet = true;
    const size_t chunk = replyChunkBytes ? replyChunkBytes : std::max<size_t>(replyBody.size(), 1);
    for (size_t at = 0; at < replyBody.size(); at += chunk) {
      if (shouldAbort && shouldAbort()) {
        aborted_ = true;
        return -1;
      }
      const size_t count = std::min(chunk, replyBody.size() - at);
      if (!onData(reinterpret_cast<const uint8_t*>(replyBody.data() + at), count)) {
        callbackAborted_ = true;
        return replyCode;
      }
      nowMs += advanceMsPerChunk;
    }
    if (shouldAbort && shouldAbort()) {
      aborted_ = true;
      return -1;
    }
    complete_ = true;
    return replyCode;
  }
  int sendRequest(const char*, const std::string&) { return replyCode; }
  const std::string& getString() const { return replyBody; }
  bool responseComplete() const { return complete_; }
  bool callbackAborted() const { return callbackAborted_; }
  bool aborted() const { return aborted_; }
  void end() {}

 private:
  std::string body_;
  bool complete_ = false;
  bool callbackAborted_ = false;
  bool aborted_ = false;
};
}  // namespace freeink
