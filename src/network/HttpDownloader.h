#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
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
    CACHE_ERROR,  // File committed, but stale reading cache could not be removed.
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

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it. progress also runs
   * while the server is silent; a true *cancelFlag then ends the transfer, as in downloadToFile.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", const char* rootCA = nullptr, bool allowRedirects = true,
                       ProgressCallback progress = nullptr, bool* cancelFlag = nullptr, TransferStats* stats = nullptr);

  /**
   * Bytes first..last of a resource over a connection of its own, pinned to rootCA, no redirects:
   * a 206 whose Content-Range starts at `first` streams its part to onData. A 200 (the whole body)
   * is taken only when first is 0, and sets *whole. Otherwise as the streaming fetchUrl above.
   */
  static bool fetchRange(const std::string& url, size_t first, size_t last, const DataCallback& onData,
                         const char* rootCA, ProgressCallback progress, bool* cancelFlag, TransferStats* stats,
                         bool* whole);

  /**
   * Download a file to the SD card with optional credentials.
   *
   * The legacy downgradeRedirectsToHttp flag is rejected by the TLS backend.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      bool downgradeRedirectsToHttp = false);
};
