#pragma once
// API boundary double for the esp_http_client fallback. The production runGet
// loop owns cancellation, timeout policy, payload delivery and cleanup.
#include <sys/socket.h>
#include <algorithm>
#include <memory>
#include <cstdlib>
#include <string>
#include <functional>
namespace wire {
struct FallbackRequest {
  std::string url;
  bool authorized;
};
inline std::vector<FallbackRequest> fallbackRequests;
}
struct FakeHttp;
using esp_http_client_handle_t = FakeHttp*;
constexpr int HTTP_EVENT_ON_HEADER = 1;
struct esp_http_client_event_t {
  void* user_data;
  esp_http_client_handle_t client;
  int event_id = 0;
  char* header_key = nullptr;
  char* header_value = nullptr;
};
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
  std::string url;
  std::string location;
  bool authorized = false;
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
    if (!h->location.empty()) {
      char key[] = "Location";
      event.event_id = HTTP_EVENT_ON_HEADER;
      event.header_key = key;
      event.header_value = h->location.data();
      h->config.event_handler(&event);
    }
    event.event_id = 0;
    h->config.event_handler(&event);
  }
}
inline FakeHttp* esp_http_client_init(esp_http_client_config_t* config) {
  auto* client = new FakeHttp{};
  client->config = *config;
  client->url = config->url;
  return client;
}
inline void esp_http_client_cleanup(FakeHttp* h) { delete h; }
inline int esp_http_client_set_header(FakeHttp* h, const char* name, const char*) {
  if (std::string(name) == "Authorization") h->authorized = true;
  return 0;
}
inline int esp_http_client_delete_header(FakeHttp* h, const char* name) {
  if (std::string(name) == "Authorization") h->authorized = false;
  return 0;
}
inline int esp_http_client_open(FakeHttp* h, int) {
  ++wire::connectAttempts;
  wire::fallbackRequests.push_back({h->url, h->authorized});
  if (wire::replies.empty()) return -1;
  h->raw = wire::replies.front();
  wire::replies.pop_front();
  h->offset = h->contentLength = h->received = 0;
  h->status = 0;
  h->chunked = h->framed = h->complete = false;
  h->location.clear();
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
  const auto location = h->raw.find("Location: ");
  if (location != std::string::npos && location < end) {
    const auto value = location + 10;
    h->location = h->raw.substr(value, h->raw.find("\r\n", value) - value);
  }
  notify(h);
  return h->contentLength;
}
inline int esp_http_client_get_status_code(FakeHttp* h) { return h->status; }
inline int esp_http_client_set_redirection(FakeHttp* h) {
  if (h->location.empty()) return -1;
  if (h->url.rfind("https://", 0) == 0 && h->location.rfind("https://", 0) != 0) return -1;
  if (h->location.find("://") != std::string::npos) h->url = h->location;
  else if (h->location.rfind("/", 0) == 0) {
    const auto authorityEnd = h->url.find('/', h->url.find("://") + 3);
    h->url = h->url.substr(0, authorityEnd) + h->location;
  } else return -1;
  return 0;
}
inline int esp_http_client_set_url(FakeHttp* h, const char* url) {
  h->url = url;
  return 0;
}
inline int esp_http_client_get_url(FakeHttp* h, char* out, int len) {
  const auto query = h->url.find('?');
  const auto url = h->url.substr(0, query);
  if (len <= 0) return -1;
  std::memcpy(out, url.c_str(), std::min(url.size(), static_cast<size_t>(len - 1)));
  out[std::min(url.size(), static_cast<size_t>(len - 1))] = '\0';
  return 0;
}
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
