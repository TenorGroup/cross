#pragma once
// A transport that only writes down what the KOSync client sends: method, URL, headers in order, body.
#include <Arduino.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace wire {
inline std::vector<std::string> transcript;  // one entry per request
inline int replyCode = 200;
inline std::string replyBody = R"({"progress":"/body/DocFragment[3]/body/p[2]/text().4","percentage":0.5,"device":"d","device_id":"i","timestamp":5,"position":{"pctQ":500000,"spineIndex":2,"pageNumber":1,"totalPages":4,"xpath":"/body/p"}})";
}  // namespace wire

namespace freeink {
class SecureHttpClient {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  using AbortCallback = std::function<bool()>;
  void setCACert(const char*) {}
  void setTimeout(uint32_t) {}
  bool begin(const std::string& url) {
    url_ = url;
    headers_.clear();
    return true;
  }
  void addHeader(const std::string& name, const std::string& value) { headers_ += "  " + name + ": " + value + "\n"; }
  int GET() { return finish("GET", ""); }
  int GET(const DataCallback& onData, const AbortCallback& = nullptr) {
    finish("GET", "");
    onData(reinterpret_cast<const uint8_t*>(wire::replyBody.data()), wire::replyBody.size());
    return wire::replyCode;
  }
  int sendRequest(const char* method, const std::string& body) { return finish(method, body); }
  const std::string& getString() const { return wire::replyBody; }
  bool responseComplete() const { return true; }
  bool callbackAborted() const { return false; }
  bool aborted() const { return false; }
  void end() {}

 private:
  int finish(const std::string& method, const std::string& body) {
    wire::transcript.push_back(method + " " + url_ + "\n" + headers_ + "  body: " + body + "\n");
    return wire::replyCode;
  }
  std::string url_, headers_;
};
}  // namespace freeink
