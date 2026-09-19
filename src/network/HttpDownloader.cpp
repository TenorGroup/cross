#include "HttpDownloader.h"

#include <Arduino.h>
#include <HalClock.h>
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
#include <string>

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

  bool poll(bool force = false) {
    if (cancelFlag && *cancelFlag) return true;
    const unsigned long now = millis();
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

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
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
                                         const char* rootCA, bool allowRedirects) {
  if (downgradeRedirectsToHttp) return HttpDownloader::HTTP_ERROR;
  WifiPowerSaveGuard psGuard;
  std::string url = startUrl;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    if (sink.poll()) return HttpDownloader::ABORTED;
    freeink::http_url::Parts parsed;
    if (!freeink::http_url::parse(url, parsed)) return HttpDownloader::HTTP_ERROR;
    if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return HttpDownloader::HTTP_ERROR;
    if (sink.poll()) return HttpDownloader::ABORTED;
    freeink::SecureHttpClient http;
    http.setTimeout(rootCA ? 10000 : HTTP_TIMEOUT_MS);
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
    if (!username.empty() && !password.empty() && freeink::http_url::sameOrigin(startUrl, url)) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET");
    const int status = http.GET(
        [&http, &sink](const uint8_t* data, size_t len) {
          if (http.getStatus() != 200) return true;
          if (sink.total == 0 && http.hasContentLength()) sink.total = http.getContentLength();
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          return true;
        },
        [&http, &sink]() {
          if (http.getStatus() == 200 && http.hasContentLength()) sink.total = http.getContentLength();
          return sink.poll();
        });

    if (http.aborted() || sink.poll(true)) return HttpDownloader::ABORTED;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed");
      return HttpDownloader::HTTP_ERROR;
    }
    if (isRedirect(status)) {
      if (!allowRedirects) return HttpDownloader::HTTP_ERROR;
      const std::string location = http.getHeader("location");
      std::string nextUrl;
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(url, location, nextUrl) ||
          !freeink::http_url::redirectAllowed(url, nextUrl)) {
        LOG_ERR("HTTP", "wolfSSL bad redirect: %d", status);
        return HttpDownloader::HTTP_ERROR;
      }
      url = std::move(nextUrl);
      continue;
    }
    if (status != 200) {
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
                                     Sink& sink, const char* rootCA, bool allowRedirects) {
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
  for (int hop = 0;
       allowRedirects && username.empty() && url.rfind("http://", 0) == 0 && isRedirect(status) && hop < MAX_REDIRECTS;
       ++hop) {
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
    esp_http_client_set_timeout_ms(client, HTTP_CONNECT_TIMEOUT_MS);
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
                                           bool allowRedirects = true) {
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
  return runGetWolf(url, username, password, sink, downgradeRedirectsToHttp, rootCA, allowRedirects);
#else
  return runGet(url, username, password, sink, rootCA, allowRedirects);
#endif
}
}  // namespace

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
                              const std::string& password, const char* rootCA, bool allowRedirects) {
  LOG_DBG("HTTP", "Fetching");
  Sink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, sink, false, rootCA, allowRedirects) == OK;
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
