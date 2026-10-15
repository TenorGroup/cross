#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <HeapFloor.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Logging.h>
#include <NetworkTrust.h>
#include <SecureHttpClient.h>
#include <base64.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderCredentialStore.h"
#include "../../src/platform/ColdLogProbe.h"

int KOReaderSyncClient::lastHttpCode = 0;
uint32_t KOReaderSyncClient::requestTimeoutMs = 0;
uint32_t KOReaderSyncClient::requestDeadlineMs = 0;

namespace {
// Device identifier for CrossPoint reader
constexpr char DEVICE_NAME[] = "tenor/cross";
constexpr char DEVICE_ID[] = "crosspoint-reader";

// wolfSSL uses the default allocator, which can use PSRAM on supported builds.
// Keep a free-space floor and room for a full TLS record when the server does
// not negotiate our smaller record limit. These are preflight margins, not a
// guarantee that a handshake will fit.
constexpr uint32_t MIN_FREE_FOR_TLS = kosyncauto::MIN_FREE_FOR_TLS;
constexpr uint32_t MIN_BLOCK_FOR_TLS = kosyncauto::MIN_BLOCK_FOR_TLS;

constexpr size_t MAX_PROGRESS_BODY = 16384;

bool deadlineReached(const uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(millis() - deadline) >= 0;
}

class ProgressBody {
 public:
  bool append(const uint8_t* data, const size_t len) {
    if (len > MAX_PROGRESS_BODY - size_) {
      tooLarge_ = true;
      return false;
    }
    const size_t needed = size_ + len;
    if (needed > capacity()) {
      size_t nextCapacity = heapfloor::kBigBytes;
      while (nextCapacity < needed && nextCapacity < MAX_PROGRESS_BODY) nextCapacity *= 2;
      auto next = heapfloor::array<uint8_t[]>(nextCapacity, "kosync-progress");
      if (!next) {
        noMemory_ = true;
        return false;
      }
      if (size_) memcpy(next.get(), bytes(), size_);
      large_ = std::move(next);
      largeCapacity_ = nextCapacity;
    }
    if (len) memcpy(writableBytes() + size_, data, len);
    size_ = needed;
    return true;
  }

  const uint8_t* bytes() const { return large_ ? large_.get() : small_.data(); }
  size_t size() const { return size_; }
  bool noMemory() const { return noMemory_; }
  bool tooLarge() const { return tooLarge_; }

 private:
  size_t capacity() const { return large_ ? largeCapacity_ : small_.size(); }
  uint8_t* writableBytes() { return large_ ? large_.get() : small_.data(); }

  std::array<uint8_t, 1024> small_{};
  std::unique_ptr<uint8_t[]> large_;
  size_t largeCapacity_ = 0;
  size_t size_ = 0;
  bool noMemory_ = false;
  bool tooLarge_ = false;
};

// Apply the shared KOSync auth headers after begin(). x-auth-* is the native
// KOSync scheme; Basic auth is added for Calibre-Web-Automated compatibility.
void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  const std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
  const String encoded = base64::encode(credentials.c_str());
  http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
}

// True when free heap is too low to risk a TLS handshake.
bool insufficientHeap() {
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_FREE_FOR_TLS || heap.largestBlockBytes < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("KOSync",
            "Insufficient allocatable heap for TLS handshake: %zu bytes free (need %u), %zu max alloc (need %u)",
            heap.freeBytes, MIN_FREE_FOR_TLS, heap.largestBlockBytes, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}
}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  LOG_DBG("KOSync", "Authenticating (heap: %u)", (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) return LOW_MEMORY;

  freeink::SecureHttpClient http;
  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(url, parsed)) return NETWORK_ERROR;
  if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return NETWORK_ERROR;
  http.setCACert(network_trust::forUrl(url));
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  const int httpCode = http.GET();
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Auth response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  // Any 2xx is success. The reference kosync server answers 200, but
  // KOSync-compatible implementations differ (BookLore/grimmory is a Spring
  // service and uses the idiomatic codes) - see issue #2876.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::createUser() {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/create";
  LOG_DBG("KOSync", "Creating account (heap: %u)", (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) return LOW_MEMORY;

  JsonDocument doc;
  doc["username"] = KOREADER_STORE.getUsername();
  doc["password"] = KOREADER_STORE.getMd5Password();
  std::string body;
  serializeJson(doc, body);

  freeink::SecureHttpClient http;
  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(url, parsed)) return NETWORK_ERROR;
  if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return NETWORK_ERROR;
  http.setCACert(network_trust::forUrl(url));
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL");
    return NETWORK_ERROR;
  }
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  http.addHeader("Content-Type", "application/json");
  const int httpCode = http.sendRequest("POST", body);
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Create user response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return OK;  // 2xx: created (see #2876)
  if (httpCode == 402) return USER_EXISTS;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url =
      KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash + "?position_kinds=locator,percentage";
  LOG_DBG("KOSync", "Getting progress (heap: %u)", (unsigned)ESP.getFreeHeap());
  const bool lowHeap = insufficientHeap();
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::HttpHeap, lowHeap ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (lowHeap) return LOW_MEMORY;

  freeink::SecureHttpClient http;
  freeink::http_url::Parts parsed;
  const bool urlOk = freeink::http_url::parse(url, parsed);
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::HttpUrl, urlOk ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (!urlOk) return NETWORK_ERROR;
  if (parsed.tls && time(nullptr) < 1735689600) {
    const bool ntpOk = halClock.syncFromNTP();
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpNtp, ntpOk ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    if (!ntpOk) return NETWORK_ERROR;
  } else {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpNtp, 2, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  }
  http.setCACert(network_trust::forUrl(url));
  const bool beginOk = http.begin(url);
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::HttpBegin, beginOk ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (!beginOk) {
    LOG_ERR("KOSync", "Bad URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  if (requestTimeoutMs) http.setTimeout(requestTimeoutMs);
  ProgressBody deadlineBody;
  const bool hasDeadline = requestDeadlineMs != 0;
#ifdef SIMULATOR
  // The simulator's Arduino HTTP adapter only exposes the buffered GET. It has no socket callback or
  // abort state, so leave the real-device deadline path out of this host-only transport shim.
  (void)deadlineBody;
  (void)hasDeadline;
  const int httpCode = http.GET();
#else
  const int httpCode = hasDeadline
                           ? http.GET([&deadlineBody](const uint8_t* data, const size_t len) {
                               return deadlineBody.append(data, len);
                             },
                             [] { return deadlineReached(KOReaderSyncClient::requestDeadlineMs); })
                           : http.GET();
#endif
  lastHttpCode = httpCode;

#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::HttpTransport, httpCode, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  restartlog::trace(restartlog::TraceStage::HttpReply, lastHttpCode, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif

  LOG_DBG("KOSync", "Get progress response: %d", httpCode);

#ifndef SIMULATOR
  if (hasDeadline && deadlineBody.noMemory()) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpBody, 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    http.end();
    return LOW_MEMORY;
  }
  if (hasDeadline && deadlineBody.tooLarge()) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpBody, 2, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    http.end();
    return JSON_ERROR;
  }
  if (httpCode <= 0 || (hasDeadline && (http.aborted() || !http.responseComplete()))) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpBody, http.aborted() ? 3 : 4, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    http.end();
    return NETWORK_ERROR;
  }
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::HttpBody, 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
#endif

  // 204 = success with no stored progress for this document (Spring-style
  // KOSync implementations; the reference server answers 200 with an empty
  // object instead). Map it to the same graceful no-remote-progress path as
  // 404 rather than falling through to SERVER_ERROR - see issue #2876.
  if (httpCode == 204) {
    http.end();
    return NOT_FOUND;
  }

  if (httpCode >= 200 && httpCode < 300) {
    JsonDocument doc;
#ifdef SIMULATOR
    const auto body = http.getString();
    const DeserializationError error = deserializeJson(doc, body.c_str(), body.length());
#else
    const std::string& body = http.getString();
    const DeserializationError error =
        hasDeadline ? deserializeJson(doc, reinterpret_cast<const char*>(deadlineBody.bytes()), deadlineBody.size())
                    : deserializeJson(doc, body.c_str(), body.size());
#endif
    http.end();

    if (error) {
#ifdef TENOR_PRESS_PROBE
      restartlog::trace(restartlog::TraceStage::HttpParse, 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
      LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
      return JSON_ERROR;
    }

#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::HttpParse, 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"] | "";  // null or absent is no name, not the word "null"
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    outProgress.position.reset();
    if (KOREADER_STORE.usesCrossPointSyncServer()) {
      const JsonObjectConst pos = doc["position"].as<JsonObjectConst>();
      if (!pos.isNull()) {
        KOReaderRichPosition rich;
        rich.pctQ = pos["pctQ"].as<uint32_t>();
        rich.spineIndex = pos["spine"].as<uint16_t>();
        rich.pageNumber = pos["page"].as<uint16_t>();
        const uint16_t pages = pos["pages"].as<uint16_t>();
        rich.totalPages = pages > 0 ? pages : 1;
        const uint16_t para = pos["para"].as<uint16_t>();
        if (para > 0) rich.paragraphIndex = para;
        rich.xpath = pos["xpath"].as<const char*>() ? pos["xpath"].as<const char*>() : "";
        LOG_DBG("KOSync", "Got rich position: spine=%u page=%u/%u para=%u", rich.spineIndex, rich.pageNumber,
                rich.totalPages, para);
        outProgress.position = std::move(rich);
      }
    }

    LOG_DBG("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  http.end();
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  LOG_DBG("KOSync", "Updating progress (heap: %u)", (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) return LOW_MEMORY;

  // Build JSON body
  JsonDocument doc;
  doc["document"] = progress.document;
  if (progress.metadata.has_value()) {
    auto meta = doc["metadata"].to<JsonObject>();
    meta["filename"] = progress.metadata->filename;
    meta["title"] = progress.metadata->title;
    meta["authors"] = progress.metadata->authors;
  }
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = DEVICE_ID;
  if (progress.position.has_value() && KOREADER_STORE.usesCrossPointSyncServer()) {
    // CrossPoint-specific extension: do not send it to third-party KOSync servers.
    const auto& p = *progress.position;
    auto pos = doc["position"].to<JsonObject>();
    pos["pctQ"] = p.pctQ;
    pos["spine"] = p.spineIndex;
    pos["page"] = p.pageNumber;
    pos["pages"] = p.totalPages;
    if (p.paragraphIndex.has_value()) pos["para"] = *p.paragraphIndex;
    // Server rejects the whole position object if xpath exceeds 120 bytes.
    if (!p.xpath.empty() && p.xpath.size() <= 120) pos["xpath"] = p.xpath;
  }

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("KOSync", "Request body: %s", body.c_str());

  freeink::SecureHttpClient http;
  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(url, parsed)) return NETWORK_ERROR;
  if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return NETWORK_ERROR;
  http.setCACert(network_trust::forUrl(url));
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  http.addHeader("Content-Type", "application/json");
  if (requestTimeoutMs) http.setTimeout(requestTimeoutMs);
  const int httpCode = http.sendRequest("PUT", body);
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Update progress response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  // Any 2xx accepts the progress. The reference kosync server answers 200,
  // but Spring-based KOSync implementations (BookLore/grimmory) answer a PUT
  // with the idiomatic 201/204, which used to land in SERVER_ERROR and made
  // every sync against them fail after a successful pull - issue #2876.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return tr(STR_SYNC_SUCCESS);
    case NO_CREDENTIALS:
      return tr(STR_NO_CREDENTIALS_MSG);
    case NETWORK_ERROR:
      return tr(STR_SYNC_NETWORK_ERROR);
    case AUTH_FAILED:
      return tr(STR_AUTH_FAILED);
    case SERVER_ERROR:
      return tr(STR_SYNC_SERVER_ERROR);
    case JSON_ERROR:
      return tr(STR_SYNC_DATA_ERROR);
    case NOT_FOUND:
      return tr(STR_SYNC_NO_PROGRESS);
    case LOW_MEMORY:
      return tr(STR_SYNC_LOW_MEMORY);
    case USER_EXISTS:
      return tr(STR_USERNAME_TAKEN);
    default:
      return tr(STR_SYNC_UNKNOWN_ERROR);
  }
}
