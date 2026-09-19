#pragma once
// API boundary double for the esp_http_client fallback. The production runGet
// loop owns cancellation, timeout policy, payload delivery and cleanup.
#include <sys/socket.h>
#include <memory>
#include <cstdlib>
#include <string>
#include <functional>
struct FakeHttp;
using esp_http_client_handle_t = FakeHttp*;
struct esp_http_client_event_t { void* user_data; esp_http_client_handle_t client; };
struct esp_http_client_config_t {
  const char* url = nullptr;
  int buffer_size = 0, buffer_size_tx = 0, timeout_ms = 0;
  const char* cert_pem = nullptr;
  void (*crt_bundle_attach)() = nullptr;
  bool keep_alive_enable = false;
  void* user_data = nullptr;
  int (*event_handler)(esp_http_client_event_t*) = nullptr;
};
struct FakeHttp {
  esp_http_client_config_t config;
  std::string raw;
  size_t offset = 0, contentLength = 0, received = 0;
  bool chunked = false, framed = false, complete = false;
  int status = 0;
};
constexpr int ESP_ERR_HTTP_EAGAIN = 7007;
inline void esp_crt_bundle_attach() {}
inline const char* esp_err_to_name(int) { return "fixture"; }
template <class T> auto makeUniqueNoThrow(size_t n) { return std::make_unique<T>(n); }
inline void notify(FakeHttp* h) {
  if (h->config.event_handler) {
    esp_http_client_event_t event{h->config.user_data, h};
    h->config.event_handler(&event);
  }
}
inline FakeHttp* esp_http_client_init(esp_http_client_config_t* config) { return new FakeHttp{*config}; }
inline void esp_http_client_cleanup(FakeHttp* h) { delete h; }
inline int esp_http_client_set_header(FakeHttp*, const char*, const char*) { return 0; }
inline int esp_http_client_open(FakeHttp* h, int) {
  ++wire::connectAttempts;
  if (wire::replies.empty()) return -1;
  h->raw = wire::replies.front();
  wire::replies.pop_front();
  return 0;
}
inline int esp_http_client_set_timeout_ms(FakeHttp* h, int timeout) { h->config.timeout_ms = timeout; return 0; }
inline int esp_http_client_get_socket(FakeHttp*) { return -1; }
inline int64_t esp_http_client_fetch_headers(FakeHttp* h) {
  const size_t end = h->raw.find("\r\n\r\n");
  if (end == std::string::npos) return -ESP_ERR_HTTP_EAGAIN;
  h->status = std::atoi(h->raw.c_str()+9);
  h->offset = end + 4;
  const auto length = h->raw.find("Content-Length: ");
  h->framed = length != std::string::npos && length < end;
  if (h->framed) h->contentLength = std::strtoul(h->raw.c_str()+length+16, nullptr, 10);
  h->chunked = h->raw.find("Transfer-Encoding: chunked") < end;
  notify(h);
  return h->contentLength;
}
inline int esp_http_client_get_status_code(FakeHttp* h) { return h->status; }
inline int esp_http_client_set_redirection(FakeHttp*) { return -1; }
inline int esp_http_client_close(FakeHttp*) { return 0; }
inline int esp_http_client_read(FakeHttp* h, char* out, int len) {
  if (h->complete || (h->framed && h->received == h->contentLength)) { h->complete = true; return 0; }
  if (h->offset >= h->raw.size()) {
    if (!h->framed && !h->chunked && wire::closeAfterReply) { h->complete = true; return 0; }
    return -ESP_ERR_HTTP_EAGAIN;
  }
  size_t n;
  if (h->chunked) {
    const auto end = h->raw.find("\r\n", h->offset);
    if (end == std::string::npos) return -ESP_ERR_HTTP_EAGAIN;
    n = std::strtoul(h->raw.c_str()+h->offset, nullptr, 16);
    if (!n) { h->complete = true; return 0; }
    if (end + 2 + n + 2 > h->raw.size() || n > static_cast<size_t>(len)) return -ESP_ERR_HTTP_EAGAIN;
    h->offset = end+2;
  } else n = std::min(static_cast<size_t>(len), h->raw.size()-h->offset);
  std::memcpy(out, h->raw.data()+h->offset, n);
  h->offset += n + (h->chunked ? 2 : 0);
  h->received += n;
  notify(h);
  return static_cast<int>(n);
}
inline bool esp_http_client_is_complete_data_received(FakeHttp* h) { return h->complete; }
