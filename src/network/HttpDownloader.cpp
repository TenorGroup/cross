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

#include <algorithm>
#include <ctime>
#include <functional>
#include <new>
#include <string>
#include <vector>

#include "WifiPowerSaveGuard.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <ResumableFetch.h>
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

// Written after every request, and read by the UI right after a failure.
int g_lastStatus = 0;
// Whether the last answer came from another origin than the request started at.
bool g_lastRedirected = false;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  const bool* cancelFlag = nullptr;
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
  bool redirected = false;

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
    // An empty password with a user is valid Basic auth (RFC 7617), as SecureHttpClient sends it.
    if (!username.empty() && freeink::http_url::sameOrigin(startUrl, url)) {
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
      sink.redirected = !freeink::http_url::sameOrigin(startUrl, nextUrl);
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
  if (!username.empty()) {
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
    sink.redirected = !freeink::http_url::sameOrigin(url, nextUrl);
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
  g_lastStatus = 0;
  g_lastRedirected = false;
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
  const HttpDownloader::DownloadError result =
      runGetWolf(url, username, password, sink, downgradeRedirectsToHttp, rootCA, allowRedirects, range, rangeClient);
#else
  const HttpDownloader::DownloadError result = runGet(url, username, password, sink, rootCA, allowRedirects, range);
#endif
  g_lastStatus = sink.status;
  g_lastRedirected = sink.redirected;
  return result;
}
}  // namespace

namespace {
// Upstream-form requests: the SDK's resumable GET, with the trust, clock and Wi-Fi rules of the pinned path
// above. Credentials never leave the origin the request started at.
constexpr size_t MAX_RESPONSE_BYTES = 4096;  // postForm: token replies are small JSON

#if defined(FREEINK_NET_WOLFSSL)
// A certificate is judged against the clock: sync it first when it is still at its cold-boot value.
bool tlsClockReady(const std::string& url) {
  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(url, parsed)) return false;
  return !parsed.tls || time(nullptr) >= 1735689600 || halClock.syncFromNTP();
}

void configureClient(freeink::SecureHttpClient& http, const char* rootCA) {
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setCACert(rootCA);
  // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would append a second User-Agent header, which
  // strict servers reject (aiohttp answers 400 "Duplicate 'User-Agent' header found").
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
}
#endif

bool wifiUp() {
#ifndef SIMULATOR
  // Native simulator sockets use the host network independently of fake Wi-Fi.
  // On the device, reject before NTP or TCP can enter an uninitialised driver.
  if (WiFi.getMode() == WIFI_MODE_NULL || WiFi.status() != WL_CONNECTED) {
    LOG_ERR("HTTP", "WiFi is not connected");
    return false;
  }
#endif
  return true;
}

// All plain GETs (pages, feeds, books) go through here. A body cut short resumes with a Range request; rewind
// restarts the sink when the server ignores the Range. The legacy https->http step-down is refused, as before.
HttpDownloader::DownloadError runGetResumable(const std::string& url, const std::string& username,
                                              const std::string& password,
                                              const std::vector<HttpDownloader::Header>& headers, Sink& sink,
                                              const std::function<bool()>& rewind = nullptr,
                                              const bool downgradeRedirectsToHttp = false) {
  g_lastStatus = 0;
  g_lastRedirected = false;
  if (downgradeRedirectsToHttp) return HttpDownloader::HTTP_ERROR;
  if (!wifiUp()) return HttpDownloader::HTTP_ERROR;
#if defined(FREEINK_NET_WOLFSSL)
  if (sink.poll()) return HttpDownloader::ABORTED;
  if (!tlsClockReady(url)) return HttpDownloader::HTTP_ERROR;
  WifiPowerSaveGuard psGuard;
  freeink::FetchOptions options;
  freeink::FetchSink fetchSink;
  fetchSink.write = [&sink](const uint8_t* data, const size_t len) { return sink.write(data, len); };
  fetchSink.rewind = rewind;
  fetchSink.progress = [&sink](const size_t bytes, const size_t total) {
    sink.downloaded = bytes;
    sink.total = total;
    sink.lastDataMs = millis();
    sink.poll();  // pumps progress; the abort callback below stops the transfer on a cancel
  };
  bool answeredBySameOrigin = true;
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        answeredBySameOrigin = sameOrigin;
        // The starting host has its own entry in the trust table; another origin (a CDN a redirect led to) gets the
        // full set, as any host without an entry does.
        configureClient(http, sameOrigin ? network_trust::forUrl(url) : network_trust::roots);
        // Credentials and caller headers stay with the starting origin; a redirect elsewhere (or to plain http)
        // gets neither.
        if (sameOrigin) {
          if (!username.empty()) http.setBasicAuth(username, password);
          for (const auto& h : headers) http.addHeader(h.first, h.second);
        }
        LOG_DBG("HTTP", "wolfSSL GET (heap %u, max block %u)", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      },
      fetchSink, [&sink] { return sink.poll(); });
  // FetchResult::status is -1 when no response arrived (bad URL, DNS, connect, TLS).
  sink.status = result.status < 0 ? 0 : result.status;
  sink.headers = result.status > 0;
  sink.redirected = !answeredBySameOrigin;
  g_lastStatus = sink.status;
  g_lastRedirected = sink.redirected;
  if (result.aborted || sink.poll(true)) return HttpDownloader::ABORTED;
  if (result.stopped) return HttpDownloader::FILE_ERROR;
  if (result.status == 401 || result.status == 403) {
    LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d", result.status);
    return HttpDownloader::UNAUTHORIZED;
  }
  if (result.status < 200 || result.status >= 300) {
    LOG_ERR("HTTP", "wolfSSL request failed: status %d", result.status);
    return HttpDownloader::HTTP_ERROR;
  }
  if (!result.complete) {
    LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", result.bytes, result.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
#else
  // esp_http_client carries no Range resume and no caller headers.
  (void)headers;
  (void)rewind;
  return runGetSecure(url, username, password, sink);
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
int HttpDownloader::lastStatus() { return g_lastStatus; }

bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password, headers);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  LOG_DBG("HTTP", "Fetching");
  Sink sink;
  sink.write = onData;
  return runGetResumable(url, username, password, headers, sink) == OK;
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

#if defined(FREEINK_NET_WOLFSSL)
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const FetchOptions& options) {
  // OPDS fetches need what fetchResumable withholds: the final HTTP status and a 401 body (auth documents are
  // served as 401 bodies). Direct GET with a manual redirect loop; catalog feeds are small enough that Range resume
  // isn't worth carrying here (books go through downloadToFile, which has it).
  g_lastStatus = 0;
  g_lastRedirected = false;
  if (!wifiUp()) {
    if (options.statusOut) *options.statusOut = 0;
    return false;
  }
  WifiPowerSaveGuard psGuard;
  std::string current = url;
  const std::string startOrigin = freeink::fetchOrigin(url);
  int status = 0;
  bool sinkStopped = false;
  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    if (!tlsClockReady(current)) {
      status = 0;
      break;
    }
    freeink::SecureHttpClient http;
    if (!http.begin(current)) {
      LOG_ERR("HTTP", "bad URL");
      status = 0;
      break;
    }
    configureClient(http, network_trust::forUrl(current));
    // Credentials belong to the starting origin only: a redirect to another origin (a CDN) must not receive them.
    if (freeink::fetchOrigin(current) == startOrigin) {
      if (!options.bearer.empty()) {
        http.addHeader("Authorization", "Bearer " + options.bearer);
      } else if (!options.username.empty() && !options.password.empty()) {
        http.setBasicAuth(options.username, options.password);
      }
      for (const auto& h : options.headers) http.addHeader(h.first, h.second);
    }
    if (!options.accept.empty()) http.addHeader("Accept", options.accept);
    if (!options.acceptLanguage.empty()) http.addHeader("Accept-Language", options.acceptLanguage);

    LOG_DBG("HTTP", "wolfSSL GET");
    status = http.GET([&](const uint8_t* data, size_t len) {
      const int st = http.getStatus();
      const bool wanted = (st >= 200 && st < 300) || (options.captureErrorBody && st == 401);
      if (!wanted) return true;  // redirect or error body: drain
      if (!onData(data, len)) {
        sinkStopped = true;
        return false;
      }
      return true;
    });
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      const std::string location = http.getHeader("location");
      std::string next;
      // The same refusals as the pinned path: no step-down to http, no unresolvable target.
      if (!resolveRedirectUrl(current, location, next) || !freeink::http_url::redirectAllowed(current, next)) break;
      current = std::move(next);
      continue;
    }
    break;
  }
  g_lastStatus = status > 0 ? status : 0;
  g_lastRedirected = freeink::fetchOrigin(current) != startOrigin;
  if (options.statusOut) *options.statusOut = g_lastStatus;
  return status >= 200 && status < 300 && !sinkStopped;
}

bool HttpDownloader::postForm(const std::string& url, const std::string& formBody, std::string& outResponse,
                              int* statusOut) {
  outResponse.clear();
  if (statusOut) *statusOut = 0;
  if (!wifiUp() || !tlsClockReady(url)) return false;
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL");
    return false;
  }
  configureClient(http, network_trust::forUrl(url));
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  LOG_DBG("HTTP", "wolfSSL POST");
  const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(formBody.data()), formBody.size(),
                                      [&outResponse](const uint8_t* data, size_t len) {
                                        const size_t room =
                                            MAX_RESPONSE_BYTES - std::min(outResponse.size(), MAX_RESPONSE_BYTES);
                                        outResponse.append(reinterpret_cast<const char*>(data), std::min(len, room));
                                        return true;
                                      });
  if (statusOut) *statusOut = status > 0 ? status : 0;
  return status >= 200 && status < 300;
}

HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& contentType, const std::string& destPath,
                                                         ProgressCallback progress, const bool* cancelFlag) {
  LOG_DBG("HTTP", "POST download");
  if (!wifiUp() || !tlsClockReady(url)) return HTTP_ERROR;
  WifiPowerSaveGuard psGuard;

  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL");
    file.close();
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }
  configureClient(http, network_trust::forUrl(url));
  http.addHeader("Content-Type", contentType);

  size_t downloaded = 0;
  bool writeFailed = false;
  const int status = http.sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        const int st = http.getStatus();
        if (st < 200 || st >= 300) return true;  // drain the error body
        if (file.write(data, len) != len) {
          writeFailed = true;
          return false;
        }
        downloaded += len;
        if (progress && http.hasContentLength()) progress(downloaded, http.getContentLength());
        return true;
      },
      [cancelFlag] { return cancelFlag && *cancelFlag; });
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would otherwise close only after the remove.
  if (file.isOpen()) file.close();

  DownloadError result = OK;
  if (http.aborted()) {
    result = ABORTED;
  } else if (writeFailed) {
    result = FILE_ERROR;
  } else if (status < 200 || status >= 300) {
    LOG_ERR("HTTP", "POST failed: status %d", status);
    result = HTTP_ERROR;
  } else if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    result = HTTP_ERROR;
  } else if (http.hasContentLength() && downloaded != http.getContentLength()) {
    LOG_ERR("HTTP", "POST response incomplete: %zu of %zu bytes", downloaded, (size_t)http.getContentLength());
    result = HTTP_ERROR;
  }
  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place");
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
}
#else
bool HttpDownloader::fetchUrl(const std::string&, const DataCallback&, const FetchOptions& options) {
  if (options.statusOut) *options.statusOut = 0;
  return false;  // the host build has no secure client
}
bool HttpDownloader::postForm(const std::string&, const std::string&, std::string& outResponse, int* statusOut) {
  outResponse.clear();
  if (statusOut) *statusOut = 0;
  return false;
}
HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string&, const std::string&, const std::string&,
                                                         const std::string&, ProgressCallback, const bool*) {
  return HTTP_ERROR;
}
#endif

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             const std::vector<Header>& headers,
                                                             bool downgradeRedirectsToHttp, bool (*validate)(const char*)) {
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
  // Reopening for write truncates: the server restarted the body from byte 0.
  const auto rewind = [&file, &staging] {
    file.close();
    return Storage.openFileForWrite("HTTP", staging.c_str(), file);
  };
  const DownloadError result = runGetResumable(url, username, password, headers, sink, rewind, downgradeRedirectsToHttp);
  const bool synced = result == OK && file.sync();
  // A failed rewind leaves no open handle.
  const bool closed = !file.isOpen() || file.close();
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
      return INVALID_EPUB;
    }
  }
  if (validate && !validate(staging.c_str())) {
    Storage.remove(staging.c_str());
    return INVALID_EPUB;
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
