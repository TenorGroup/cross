#pragma once
#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "Client.h"
namespace wire {
struct Request {
  std::string host;
  uint16_t port;
  std::string bytes;
};
inline std::deque<std::string> replies;
inline std::vector<Request> requests;
inline void reset() {
  replies.clear();
  requests.clear();
}
}  // namespace wire
class WiFiClient : public Client {
  std::string response;
  size_t offset = 0, index = 0;
  bool open = false;

 public:
  void setConnectionTimeout(unsigned long) {}
  int connect(IPAddress, uint16_t) override { return 0; }
  int connect(const char* host, uint16_t port) override {
    if (wire::replies.empty()) return 0;
    response = wire::replies.front();
    wire::replies.pop_front();
    offset = 0;
    open = true;
    index = wire::requests.size();
    wire::requests.push_back({host, port, {}});
    return 1;
  }
  size_t write(uint8_t v) override { return write(&v, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    if (!open) return 0;
    wire::requests[index].bytes.append((const char*)p, n);
    return n;
  }
  int available() override { return open ? response.size() - offset : 0; }
  int read() override { return available() ? static_cast<unsigned char>(response[offset++]) : -1; }
  int read(uint8_t* p, size_t n) override {
    n = std::min(n, static_cast<size_t>(available()));
    std::memcpy(p, response.data() + offset, n);
    offset += n;
    return n;
  }
  int peek() override { return available() ? static_cast<unsigned char>(response[offset]) : -1; }
  void flush() override {}
  void stop() override { open = false; }
  uint8_t connected() override { return open; }
  operator bool() override { return open; }
};
