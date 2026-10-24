#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace freeink { class SecureHttpClient; }

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
  class RangeSession {
   public:
    RangeSession();
    ~RangeSession();
    bool hasTlsContext() const;
    RangeSession(const RangeSession&) = delete;
    RangeSession& operator=(const RangeSession&) = delete;
   private:
    friend class HttpDownloader;
#if defined(FREEINK_NET_WOLFSSL)
    std::unique_ptr<freeink::SecureHttpClient> client_;
#endif
  };
  // Also called periodically during transport waits to pump activity input.
  // total == 0 means unknown size; callers must throttle repaint separately.
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    CACHE_ERROR,   // File committed, but stale reading cache could not be removed.
    INVALID_EPUB,  // Arrived whole, but is not a readable EPUB; nothing was replaced.
    UNAUTHORIZED,  // 401/403: callers holding a refreshable credential can retry
  };

  // Pre-flight floor for starting a TLS transfer. Below this the session or
  // its ~17KB record buffer fails mid-stream (wolfSSL MEMORY_E) - or an
  // interior allocation abort()s the device. Callers should check before
  // downloadToFile() and fail into their error UI instead.
  static constexpr uint32_t MIN_TLS_FREE_HEAP = 40000;
  static constexpr uint32_t MIN_TLS_MAX_ALLOC = 20000;
  // A fetch pinned to its own root CA (OTA) gives up after this long without a byte: TLS
  // handshake, response headers, or between two body reads.
  static constexpr uint32_t PINNED_CA_TIMEOUT_MS = 10000;

  // What a streaming fetch saw, for callers that record failures (OTA). The heap is sampled
  // while the connection is up, every 200 ms at most, so it describes the transfer rather
  // than the heap after the TLS session is freed.
  struct TransferStats {
    int status = 0;        // status line received, 0 when none arrived
    bool headers = false;  // the response headers were read whole
    uint32_t bytes = 0;
    uint32_t total = 0;
    uint32_t elapsedMs = 0;
    uint32_t idleMs = 0;  // since the last body byte, or since the start when none came
    uint32_t heap = 0;
    uint32_t largest = 0;
    uint32_t largestMin = 0;
  };

  using Header = std::pair<std::string, std::string>;

  /**
   * HTTP status of the last request, or 0 when it never got a response (DNS,
   * connect, TLS, timeout). fetchUrl() returns only success or failure; this
   * lets a caller tell a dead server from a catalog that wants a password.
   * Not thread-safe by design: one fetch runs at a time.
   */
  static int lastStatus();

  /**
   * True when the last answer came from a different origin (scheme, host or
   * port) than the URL asked for, after a redirect. Credentials are withheld
   * from such a hop, so a 401 there is not a wrong password.
   */
  static bool lastAnswerRedirected();

  /**
   * Fetch text content from a URL with optional credentials. `headers` are
   * added to the request, as for downloadToFile().
   */
  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", const std::vector<Header>& headers = {});

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", const std::vector<Header>& headers = {});

  /**
   * Pinned-root fetch (OTA, time zone, tenor-only callers): rootCA, when set, is the only trust anchor and
   * allowRedirects false refuses any redirect. rootCA has no default so a call with only user and password
   * stays the headers overload above. progress also runs while the server is silent; a true *cancelFlag
   * ends the transfer, as in downloadToFile. Without rootCA the chain is judged by network_trust::forUrl.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                       const std::string& password, const char* rootCA, bool allowRedirects = true,
                       ProgressCallback progress = nullptr, bool* cancelFlag = nullptr, TransferStats* stats = nullptr);

  /**
   * Bytes first..last of a resource over a connection of its own, pinned to rootCA, no redirects:
   * a 206 whose Content-Range starts at `first` streams its part to onData. A 200 (the whole body)
   * is taken only when first is 0, and sets *whole. Otherwise as the pinned-root fetchUrl above.
   */
  static bool fetchRange(const std::string& url, size_t first, size_t last, const DataCallback& onData,
                         const char* rootCA, ProgressCallback progress, bool* cancelFlag, TransferStats* stats,
                         bool* whole, RangeSession* session = nullptr);

  /**
   * Authentication and response inspection options for fetchUrl/postForm.
   * `bearer`, when set, sends "Authorization: Bearer <token>" and overrides
   * Basic credentials. `statusOut` receives the final HTTP status (0 when the
   * request never reached a response). `captureErrorBody` also streams a 401
   * response body to the callback - OPDS authentication documents are served
   * as 401 bodies.
   */
  struct FetchOptions {
    std::string username;
    std::string password;
    std::string bearer;
    // Accept header for content negotiation (e.g. preferring OPDS 2.0 JSON
    // from servers that also speak Atom); empty sends none.
    std::string accept;
    // Accept-Language header, so servers that localize the catalog return it
    // in the reader's UI language; empty sends none.
    std::string acceptLanguage;
    // Extra headers (per-server custom headers), sent to the starting origin only.
    std::vector<Header> headers;
    int* statusOut = nullptr;
    bool captureErrorBody = false;
  };

  static bool fetchUrl(const std::string& url, const DataCallback& onData, const FetchOptions& options);

  /**
   * POST an application/x-www-form-urlencoded body (e.g. an OAuth token
   * request) and collect the response body (any status) into outResponse,
   * capped at a few KB. Returns true when the response status is 2xx.
   */
  static bool postForm(const std::string& url, const std::string& formBody, std::string& outResponse,
                       int* statusOut = nullptr);

  /**
   * Download a file to the SD card with optional credentials. `headers` are
   * added to the request (e.g. a Bearer Authorization), alongside any Basic
   * auth derived from username/password.
   *
   * The legacy downgradeRedirectsToHttp flag is rejected by the TLS backend.
   * validate, when given, judges the staged file before it replaces destPath: false is INVALID_EPUB.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, const bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      const std::vector<Header>& headers = {}, bool downgradeRedirectsToHttp = false,
                                      bool (*validate)(const char* path) = nullptr);

  /**
   * POST a body (e.g. an LCP license to the fulfillment service) and stream
   * the response to the SD card, staged through "<dest>.part" like
   * downloadToFile. Single attempt, no redirects or Range resume: the
   * endpoint is ours and answers directly.
   */
  static DownloadError postToFile(const std::string& url, const std::string& body, const std::string& contentType,
                                  const std::string& destPath, ProgressCallback progress = nullptr,
                                  const bool* cancelFlag = nullptr);
};
