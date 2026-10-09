#include "HttpDownloader.h"

#include <Arduino.h>
#include <HalClock.h>
#include "../../freeink-sdk/libs/network/SecureNet/include/HttpUrl.h"
#include <Logging.h>
#include <Memory.h>
#include <NetworkTrust.h>
#include <WiFi.h>
#include <base64.h>
#include <esp_wifi.h>
#include <ZipFile.h>

#include "WebDavReplace.h"
#include "util/BookCacheUtils.h"

#include <ctime>
#include <functional>
#include <new>
#include <string>
#include <vector>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
#else
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#endif

namespace {
#if !defined(FREEINK_NET_WOLFSSL)
// RX holds the response headers. Smaller buffers leave enough contiguous heap
// for mbedTLS on redirect-heavy OPDS feeds while still preserving the headers
// we read directly (Location, Content-Length).
constexpr int HTTP_RX_BUF = 2048;
constexpr int HTTP_TX_BUF = 512;
#endif
// Reject oversized Location targets before handing them to the SDK.
constexpr size_t MAX_REDIRECT_URL = 2048;
// Keep slow-server tolerance separate from input polling. The wolfSSL waits
// poll cooperatively; esp_http_client reads retry short timeouts below.
constexpr int HTTP_TIMEOUT_MS = 60000;
#if !defined(FREEINK_NET_WOLFSSL)
constexpr int HTTP_CONNECT_TIMEOUT_MS = 3000;
constexpr int HTTP_POLL_TIMEOUT_MS = 0;
#endif
constexpr size_t READ_CHUNK = 1024;
constexpr int MAX_REDIRECTS = 5;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  unsigned long lastPumpMs = 0;
  bool pumped = false;
#if !defined(FREEINK_NET_WOLFSSL)
  std::string redirectLocation;
  bool redirectTooLong = false;
#endif
  // Transfer record for HttpDownloader::TransferStats; heap sampled only when track is set.
  bool track = false;
  int status = 0;
  bool headers = false;
  unsigned long startMs = millis();
  unsigned long lastDataMs = 0;
  unsigned long sampledMs = 0;
  uint32_t heap = 0;
  uint32_t largest = 0;
  uint32_t largestMin = 0;

  bool poll(bool force = false) {
    if (cancelFlag && *cancelFlag) return true;
    const unsigned long now = millis();
#ifndef SIMULATOR
    // Not on the forced poll after the request: the TLS session is already freed there.
    if (track && !force && (heap == 0 || now - sampledMs >= 200)) {
      sampledMs = now;
      heap = ESP.getFreeHeap();
      largest = ESP.getMaxAllocHeap();
      if (largestMin == 0 || largest < largestMin) largestMin = largest;
    }
#endif
    if (progress && (force || !pumped || now - lastPumpMs >= 25)) {
      lastPumpMs = now;
      pumped = true;
      // total == 0 is indeterminate. This callback also pumps activity input
      // while waiting for headers or the next body bytes.
      progress(downloaded, total);
    }
    return cancelFlag && *cancelFlag;
  }
};

// Bytes first..last of a resource. whole: the server sent the whole body instead (a 200), which
// is taken only for a range starting at 0.
struct ByteRange {
  size_t first = 0;
  size_t last = 0;
  bool whole = false;
};

// "bytes <first>-<last>/<size>": the part a 206 carries starts where it was asked to.
bool contentRangeStartsAt(const std::string& value, const size_t first) {
  if (value.compare(0, 6, "bytes ") != 0) return false;
  size_t start = 0;
  size_t i = 6;
  if (i >= value.size() || value[i] < '0' || value[i] > '9') return false;
  for (; i < value.size() && value[i] >= '0' && value[i] <= '9'; ++i) start = start * 10 + (value[i] - '0');
  return i < value.size() && value[i] == '-' && start == first;
}

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool resolveRedirectUrl(const std::string& base, const std::string& location, std::string& resolved) {
  freeink::http_url::Parts current;
  if (location.empty() || location.size() > MAX_REDIRECT_URL || !freeink::http_url::parse(base, current)) return false;
  const size_t authorityEnd = base.find_first_of("/?#", base.find("://") + 3);
  const std::string origin = base.substr(0, authorityEnd);
  std::string path(current.target.substr(0, current.target.find('?')));
  if (path.empty()) path = "/";
  const size_t firstDelimiter = location.find_first_of(":/?#");
  if (firstDelimiter != std::string::npos && location[firstDelimiter] == ':') resolved = location;
  else if (location.rfind("//", 0) == 0) resolved = std::string(current.scheme) + ":" + location;
  else if (location[0] == '/') resolved = origin + location;
  else if (location[0] == '?') resolved = origin + path + location;
  else if (location[0] == '#') resolved = base;
  else resolved = origin + path.substr(0, path.rfind('/') + 1) + location;

  freeink::http_url::Parts next;
  if (!freeink::http_url::parse(resolved, next)) return false;
  const size_t pathStart = resolved.find_first_of("/?#", resolved.find("://") + 3);
  std::string target(next.target);
  const size_t queryStart = target.find('?');
  std::string nextPath = target.substr(0, queryStart);
  if (nextPath.empty()) nextPath = "/";
  std::vector<std::string_view> segments;
  for (size_t start = 1; start <= nextPath.size();) {
    const size_t end = nextPath.find('/', start);
    const std::string_view segment(nextPath.data() + start,
                                   (end == std::string::npos ? nextPath.size() : end) - start);
    if (segment == "..") {
      if (!segments.empty()) segments.pop_back();
      if (end == std::string::npos) segments.push_back("");
    } else if (segment == ".") {
      if (end == std::string::npos) segments.push_back("");
    } else {
      segments.push_back(segment);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  std::string normalized = "/";
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i) normalized += '/';
    normalized.append(segments[i]);
  }
  resolved = resolved.substr(0, pathStart) + normalized +
             (queryStart == std::string::npos ? "" : target.substr(queryStart));
  return resolved.size() <= MAX_REDIRECT_URL;
}

// OtaUpdater.cpp already disables WiFi power-save for firmware downloads, but
// OPDS feed/book fetches never did despite being able to run just as long for
// a large category. Modem sleep periodically powers the radio down between
// DTIM beacon intervals, which can drop or stall packets mid-transfer -- more
// likely to be hit the longer a transfer takes, so small feeds mostly get
// away with it while a large category consistently doesn't.
struct WifiPowerSaveGuard {
  bool changed = false;
  WifiPowerSaveGuard() {
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    changed = err == ESP_OK;
    if (err != ESP_OK) LOG_ERR("HTTP", "Failed to disable WiFi power-save: %d", err);
  }
  ~WifiPowerSaveGuard() {
    if (!changed) return;
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) LOG_ERR("HTTP", "Failed to restore WiFi power-save: %d", err);
  }
};

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolf(const std::string& startUrl, const std::string& username,
                                         const std::string& password, Sink& sink, bool downgradeRedirectsToHttp,
                                         const char* rootCA, bool allowRedirects, ByteRange* range = nullptr,
                                         freeink::SecureHttpClient* rangeClient = nullptr) {
  if (downgradeRedirectsToHttp) return HttpDownloader::HTTP_ERROR;
  WifiPowerSaveGuard psGuard;
  std::string url = startUrl;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    if (sink.poll()) return HttpDownloader::ABORTED;
    freeink::http_url::Parts parsed;
    if (!freeink::http_url::parse(url, parsed)) return HttpDownloader::HTTP_ERROR;
    if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return HttpDownloader::HTTP_ERROR;
    if (sink.poll()) return HttpDownloader::ABORTED;
    freeink::SecureHttpClient localHttp;
    auto& http = rangeClient ? *rangeClient : localHttp;
    if (range) http.setReuse(false);
    http.setTimeout(rootCA ? HttpDownloader::PINNED_CA_TIMEOUT_MS : HTTP_TIMEOUT_MS);
    if (rootCA) {
      if (url.rfind("https://", 0) != 0) return HttpDownloader::HTTP_ERROR;
      http.setCACert(rootCA);
    } else
      http.setCACert(network_trust::forUrl(url));
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL bad URL");
      return HttpDownloader::HTTP_ERROR;
    }
    // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
    // append a second User-Agent header, which strict servers reject (aiohttp
    // answers 400 "Duplicate 'User-Agent' header found").
    http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (range) http.addHeader("Range", "bytes=" + std::to_string(range->first) + "-" + std::to_string(range->last));
    if (!username.empty() && !password.empty() && freeink::http_url::sameOrigin(startUrl, url)) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET");
    bool rangeChecked = false;
    const int status = http.GET(
        [&http, &sink, range, &rangeChecked](const uint8_t* data, size_t len) {
          const int code = http.getStatus();
          if (code != 200 && !(range && code == 206)) return true;
          if (range && !rangeChecked) {
            // A part that starts elsewhere, or a whole body after byte 0, cannot continue the stream.
            rangeChecked = true;
            range->whole = code == 200;
            if (range->whole ? range->first != 0 : !contentRangeStartsAt(http.getHeader("content-range"), range->first))
              return false;
          }
          if (sink.total == 0 && http.hasContentLength()) sink.total = http.getContentLength();
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          sink.lastDataMs = millis();
          return true;
        },
        [&http, &sink]() {
          if (http.getStatus() == 200 && http.hasContentLength()) sink.total = http.getContentLength();
          return sink.poll();
        });

    sink.status = http.getStatus();
    sink.headers = status >= 0;
#ifdef TENOR_PRESS_PROBE
    if (range)
      LOG_INF("HTTP_CLOSE", "first=%u peer=%d free=%u largest=%u min=%u", static_cast<unsigned>(range->first),
              http.peerCloseComplete(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
#endif
    if (http.aborted() || sink.poll(true)) return HttpDownloader::ABORTED;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed");
      return HttpDownloader::HTTP_ERROR;
    }
    if (isRedirect(status)) {
      if (!allowRedirects) return HttpDownloader::HTTP_ERROR;
      const std::string location = http.getHeader("location");
      std::string nextUrl;
      if (!resolveRedirectUrl(url, location, nextUrl) ||
          !freeink::http_url::redirectAllowed(url, nextUrl)) {
        LOG_ERR("HTTP", "wolfSSL bad redirect: %d", status);
        return HttpDownloader::HTTP_ERROR;
      }
      url = std::move(nextUrl);
      continue;
    }
    if (status != 200 && !(range && status == 206)) {
      LOG_ERR("HTTP", "wolfSSL unexpected status: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) return HttpDownloader::FILE_ERROR;
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }
    return HttpDownloader::OK;
  }
  LOG_ERR("HTTP", "too many redirects");
  return HttpDownloader::HTTP_ERROR;
}
#endif

#if !defined(FREEINK_NET_WOLFSSL)
// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink, const char* rootCA, bool allowRedirects, ByteRange* range = nullptr) {
  // No ranges on this transport: only a range from byte 0, answered with the whole body.
  if (range && range->first != 0) return HttpDownloader::HTTP_ERROR;
  if (range) range->whole = true;
  WifiPowerSaveGuard psGuard;
  if (sink.poll()) return HttpDownloader::ABORTED;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = HTTP_CONNECT_TIMEOUT_MS;
  // Verify HTTPS against the bundled CA roots. This build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified TLS handshake can't be set
  // up at all; the model is public servers over verified https and local
  // servers over plain http (esp_http_client picks the transport from the URL
  // scheme, so http:// needs no cert config). The prior setInsecure() worked
  // only because Arduino's ssl_client drives mbedtls directly.
  if (rootCA) {
    if (url.rfind("https://", 0) != 0) return HttpDownloader::HTTP_ERROR;
    config.cert_pem = rootCA;
  } else
    config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;
  config.user_data = &sink;
  config.event_handler = [](esp_http_client_event_t* event) -> esp_err_t {
    auto& target = *static_cast<Sink*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        freeink::http_url::equalFolded(event->header_key, "Location")) {
      size_t length = 0;
      while (length <= MAX_REDIRECT_URL && event->header_value[length]) ++length;
      target.redirectTooLong = length > MAX_REDIRECT_URL;
      if (!target.redirectTooLong) target.redirectLocation.assign(event->header_value, length);
    }
    if (target.poll()) {
      // Interrupt the socket without freeing parser buffers from its own
      // callback. The owning loop performs cleanup after the API unwinds.
      const int fd = esp_http_client_get_socket(event->client);
      if (fd >= 0) shutdown(fd, SHUT_RDWR);
    }
    return ESP_OK;
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!username.empty() && !password.empty()) {
    // Preemptive Basic auth, like the prior addHeader; don't wait for a 401.
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }

  // open()/read() does not auto-follow redirects (only perform() does), so step
  // 30x responses manually. OPDS download endpoints and the GitHub release CDN
  // both redirect.
  esp_err_t err = esp_http_client_open(client, 0);
  if (sink.poll()) {
    esp_http_client_cleanup(client);
    return HttpDownloader::ABORTED;
  }
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }
  // A nonblocking read returns EAGAIN between packets, giving input its own
  // cadence even for a byte-at-a-time header or an unknown-length body.
  esp_http_client_set_timeout_ms(client, HTTP_POLL_TIMEOUT_MS);
  const auto fetchHeaders = [&]() -> int64_t {
    const unsigned long deadline = millis() + HTTP_TIMEOUT_MS;
    for (;;) {
      if (sink.poll()) return -ESP_ERR_HTTP_EAGAIN;
      const int64_t result = esp_http_client_fetch_headers(client);
      if (result != -ESP_ERR_HTTP_EAGAIN || static_cast<int32_t>(millis() - deadline) >= 0) return result;
      delay(10);
    }
  };
  int64_t contentLength = fetchHeaders();
  if (sink.poll()) {
    esp_http_client_cleanup(client);
    return HttpDownloader::ABORTED;
  }
  int status = esp_http_client_get_status_code(client);
#if !defined(SIMULATOR) || defined(HTTP_DOWNLOADER_TRANSPORT_FIXTURE)
  std::string currentUrl = url;
  for (int hop = 0; allowRedirects && isRedirect(status) && hop < MAX_REDIRECTS; ++hop) {
    std::string nextUrl;
    if (sink.redirectTooLong || !resolveRedirectUrl(currentUrl, sink.redirectLocation, nextUrl) ||
        !freeink::http_url::redirectAllowed(currentUrl, nextUrl)) {
      LOG_ERR("HTTP", "unsafe redirect: %d", status);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    currentUrl = std::move(nextUrl);
    esp_http_client_close(client);
    if (esp_http_client_set_url(client, currentUrl.c_str()) != ESP_OK) {
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (!freeink::http_url::sameOrigin(url, currentUrl)) {
      esp_http_client_delete_header(client, "Authorization");
    }
    esp_http_client_set_timeout_ms(client, HTTP_CONNECT_TIMEOUT_MS);
    sink.redirectLocation.clear();
    sink.redirectTooLong = false;
    err = esp_http_client_open(client, 0);
    if (sink.poll()) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    esp_http_client_set_timeout_ms(client, HTTP_POLL_TIMEOUT_MS);
    contentLength = fetchHeaders();
    if (sink.poll()) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    status = esp_http_client_get_status_code(client);
  }
#endif

  sink.status = status;
  sink.headers = contentLength >= 0;
  if (contentLength < 0 || status != 200) {
    LOG_ERR("HTTP", "unexpected status: %d", status);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  // Unknown lengths remain zero and progress still carries received bytes.
  sink.total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("HTTP", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  unsigned long bodyDeadline = millis() + HTTP_TIMEOUT_MS;
  while (true) {
    if (sink.poll()) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (sink.poll()) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    if (read == -ESP_ERR_HTTP_EAGAIN && static_cast<int32_t>(millis() - bodyDeadline) < 0) {
      delay(10);
      continue;
    }
    if (read < 0) {
      LOG_ERR("HTTP", "read error after %zu bytes", sink.downloaded);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    bodyDeadline = millis() + HTTP_TIMEOUT_MS;
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    sink.lastDataMs = millis();
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);
  if (sink.poll(true)) return HttpDownloader::ABORTED;
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif  // !FREEINK_NET_WOLFSSL

// All HTTP(S) fetches go through wolfSSL when it is the active TLS stack: it
// speaks TLS 1.3 and reads large bodies from servers where the esp_http_client/
// mbedTLS path fails to connect or stalls mid-stream. Plain-http URLs still use a
// WiFiClient inside runGetWolf, so this is safe for non-TLS targets too.
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password, Sink& sink,
                                           bool downgradeRedirectsToHttp = false, const char* rootCA = nullptr,
                                           bool allowRedirects = true, ByteRange* range = nullptr,
                                           freeink::SecureHttpClient* rangeClient = nullptr) {
  if (downgradeRedirectsToHttp) return HttpDownloader::HTTP_ERROR;
#ifndef SIMULATOR
  // Native simulator sockets use the host network independently of fake Wi-Fi.
  // On the device, reject before NTP or TCP can enter an uninitialised driver.
  if (WiFi.getMode() == WIFI_MODE_NULL || WiFi.status() != WL_CONNECTED) {
    LOG_ERR("HTTP", "WiFi is not connected");
    return HttpDownloader::HTTP_ERROR;
  }
#endif
#if defined(FREEINK_NET_WOLFSSL)
  return runGetWolf(url, username, password, sink, downgradeRedirectsToHttp, rootCA, allowRedirects, range, rangeClient);
#else
  return runGet(url, username, password, sink, rootCA, allowRedirects, range);
#endif
}
}  // namespace

HttpDownloader::RangeSession::RangeSession() = default;
HttpDownloader::RangeSession::~RangeSession() = default;
bool HttpDownloader::RangeSession::hasTlsContext() const {
#if defined(FREEINK_NET_WOLFSSL)
  return client_ && client_->hasTlsContext();
#else
  return false;
#endif
}

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching");
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching");
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    constexpr size_t MAX_BUFFERED_RESPONSE = 64 * 1024;
    if (len > MAX_BUFFERED_RESPONSE - outContent.size()) return false;
#ifndef SIMULATOR
    if (len > outContent.capacity() - outContent.size() && ESP.getMaxAllocHeap() < 2 * (outContent.size() + len) + 8192)
      return false;
#endif
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, const char* rootCA, bool allowRedirects,
                              ProgressCallback progress, bool* cancelFlag, TransferStats* stats) {
  LOG_DBG("HTTP", "Fetching");
  Sink sink;
  sink.write = onData;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.track = stats != nullptr;
  const bool ok = runGetSecure(url, username, password, sink, false, rootCA, allowRedirects) == OK;
  if (stats) {
    const unsigned long now = millis();
    stats->status = sink.status;
    stats->headers = sink.headers;
    stats->bytes = sink.downloaded;
    stats->total = sink.total;
    stats->elapsedMs = now - sink.startMs;
    stats->idleMs = now - (sink.lastDataMs ? sink.lastDataMs : sink.startMs);
    stats->heap = sink.heap;
    stats->largest = sink.largest;
    stats->largestMin = sink.largestMin;
  }
  return ok;
}

bool HttpDownloader::fetchRange(const std::string& url, const size_t first, const size_t last,
                                const DataCallback& onData, const char* rootCA, ProgressCallback progress,
                                bool* cancelFlag, TransferStats* stats, bool* whole, RangeSession* session) {
  LOG_DBG("HTTP", "Fetching range");
  Sink sink;
  sink.write = onData;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.track = stats != nullptr;
  ByteRange range;
  range.first = first;
  range.last = last;
  freeink::SecureHttpClient* client = nullptr;
#if defined(FREEINK_NET_WOLFSSL)
  if (session) {
    if (!session->client_) {
      session->client_.reset(new (std::nothrow) freeink::SecureHttpClient());
      if (!session->client_) return false;
      session->client_->setReuseTlsContext(true);
    }
    client = session->client_.get();
  }
#endif
  const bool ok = runGetSecure(url, "", "", sink, false, rootCA, false, &range, client) == OK;
  if (whole) *whole = range.whole;
  if (stats) {
    const unsigned long now = millis();
    stats->status = sink.status;
    stats->headers = sink.headers;
    stats->bytes = sink.downloaded;
    stats->total = sink.total;
    stats->elapsedMs = now - sink.startMs;
    stats->idleMs = now - (sink.lastDataMs ? sink.lastDataMs : sink.startMs);
    stats->heap = sink.heap;
    stats->largest = sink.largest;
    stats->largestMin = sink.largestMin;
  }
  return ok;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             bool downgradeRedirectsToHttp) {
  LOG_DBG("HTTP", "Downloading file");

  if (!webdav::recoverFile(Storage, destPath.c_str())) return FILE_ERROR;
  const std::string staging = destPath + ".davtmp";
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", staging.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open staging file");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };
  const DownloadError result = runGetSecure(url, username, password, sink, downgradeRedirectsToHttp);
  const bool synced = result == OK && file.sync();
  const bool closed = file.close();
  if (result != OK || !synced || !closed || sink.downloaded == 0) {
    Storage.remove(staging.c_str());
    return result != OK ? result : (!synced || !closed ? FILE_ERROR : HTTP_ERROR);
  }
  // OPDS names books as .epub. Reuse the ZIP reader to reject a response that
  // lacks the container entry before replacing an existing readable book.
  if (destPath.size() >= 5 && destPath.compare(destPath.size() - 5, 5, ".epub") == 0) {
    ZipFile zip(staging);
    size_t containerSize = 0;
    const bool valid = zip.open() && zip.getInflatedFileSize("META-INF/container.xml", &containerSize) && containerSize > 0;
    zip.close();
    if (!valid) {
      Storage.remove(staging.c_str());
      return FILE_ERROR;
    }
  }
  bool backupCleanupPending = false;
  if (!webdav::replaceFile(Storage, staging.c_str(), destPath.c_str(), &backupCleanupPending)) {
    Storage.remove(staging.c_str());
    return FILE_ERROR;
  }
  if (!clearBookCache(destPath)) {
    LOG_ERR("HTTP", "Committed %s; reading cache cleanup failed", destPath.c_str());
    return CACHE_ERROR;
  }
  if (backupCleanupPending) LOG_ERR("HTTP", "Committed %s; backup retained", destPath.c_str());
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
