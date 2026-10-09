#pragma once
#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
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
inline int connectAttempts = 0;
inline bool closeAfterReply = false;
inline bool modelTimeWait = false;
inline int livePcbs = 0;
inline std::vector<std::unique_ptr<uint8_t[]>> timeWaitPcbs;
inline void reset() {
  replies.clear();
  requests.clear();
  connectAttempts = 0;
  closeAfterReply = false;
  modelTimeWait = false;
  livePcbs -= static_cast<int>(timeWaitPcbs.size());
  timeWaitPcbs.clear();
}
}  // namespace wire
class WiFiClient : public Client {
  std::string response;
  size_t offset = 0, index = 0;
  bool open = false;
  int closePolls = 0;
  std::unique_ptr<uint8_t[]> pcb;

 public:
  void setConnectionTimeout(unsigned long) {}
  int connect(IPAddress, uint16_t) override { return 0; }
  int connect(const char* host, uint16_t port) override {
    ++wire::connectAttempts;
    if (wire::replies.empty()) return 0;
    response = wire::replies.front();
    wire::replies.pop_front();
    offset = 0;
    closePolls = 0;
    open = true;
    if (wire::modelTimeWait) {
      pcb = std::make_unique<uint8_t[]>(200);
      ++wire::livePcbs;
    }
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
  void stop() override {
    if (pcb) {
      if (connected()) wire::timeWaitPcbs.push_back(std::move(pcb));
      else { pcb.reset(); --wire::livePcbs; }
    }
    open = false;
  }
  uint8_t connected() override {
    if (!open) return 0;
    const bool peerClosed = wire::closeAfterReply ||
        (wire::modelTimeWait && wire::requests[index].bytes.find("Connection: close\r\n") != std::string::npos);
    return open && (!peerClosed || offset < response.size() || (wire::modelTimeWait && closePolls++ < 3));
  }
  operator bool() override { return open; }
};
