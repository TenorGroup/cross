#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace freeink {
class SecureHttpClient;
}

/**
 * Optional document metadata sent alongside progress sync requests.
 * Mirrors the metadata object added in KOReader PR #15306 and adds bounded
 * bibliographic fields for compatible custom servers.
 */
struct KOReaderMetadata {
  std::string filename;  // e.g. "my_book.epub"
  std::string title;     // Document title from EPUB metadata
  std::string authors;   // Author(s) from EPUB metadata
  // Raw JSON of the book's plugin sidecar ("<book>.meta.json"): e.g. a
  // service book id a plugin recorded at download time, so a custom sync
  // server can route progress to that service. Its flat fields are sent
  // verbatim (JSON types kept) inside the metadata object; the firmware
  // assigns no meaning to the keys.
  std::string extraJson;
  std::string isbn;    // ISBN from package metadata when available
  std::string asin;    // Amazon ASIN from package metadata when available
  std::string series;  // Series/collection title when available
  std::optional<float> seriesIndex;
};

/**
 * Rich CrossPoint position sent alongside progress uploads. Maps 1:1 onto the
 * crosspoint-sync extended `position` object (see crosspoint-sync docs/API.md).
 * It is transmitted only when the selected server type supports rich progress.
 * These fields remain layout-dependent compatibility hints; the standard XPath
 * is the content anchor.
 */
struct KOReaderRichPosition {
  uint32_t pctQ = 0;                       // Percentage quantized 0..1,000,000 (metadata/fallback)
  uint16_t spineIndex = 0;                 // Spine (chapter) index
  uint16_t pageNumber = 0;                 // Page within spine (layout-dependent hint)
  uint16_t totalPages = 1;                 // Spine page count (layout-dependent hint)
  std::optional<uint16_t> paragraphIndex;  // Synthetic 1-based paragraph index
  std::string xpath;                       // KOReader-style xpath (server cap: 120 bytes)
};

/**
 * Progress data from KOReader sync server.
 */
struct KOReaderProgress {
  std::string document;                          // Document hash
  std::string progress;                          // XPath-like progress string
  float percentage;                              // Progress percentage (0.0 to 1.0)
  std::string device;                            // Device name
  std::string deviceId;                          // Device ID
  int64_t timestamp;                             // Unix timestamp of last update
  std::optional<KOReaderMetadata> metadata;      // Optional document metadata
  std::optional<KOReaderRichPosition> position;  // Optional rich position
};

/**
 * HTTP client for KOReader sync API.
 *
 * Default base URL: https://kosync.tenor.vn/
 *
 * API Endpoints:
 *   GET /users/auth - Authenticate (validate credentials)
 *   GET /syncs/progress/:document - Get progress for a document
 *   PUT /syncs/progress - Update progress for a document
 *
 * Authentication:
 *   x-auth-user: username
 *   x-auth-key: MD5 hash of password
 */
class KOReaderSyncClient {
 public:
  enum Error {
    OK = 0,
    NO_CREDENTIALS,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    JSON_ERROR,
    NOT_FOUND,
    LOW_MEMORY,
    USER_EXISTS
  };

  /**
   * Authenticate with the sync server (validate credentials).
   * @return OK on success, error code on failure
   */
  static Error authenticate();

  /**
   * Register a new account on the sync server using the stored credentials
   * (POST /users/create with the MD5 auth key - the server never sees the
   * plain password).
   * @return OK on success, USER_EXISTS if the username is taken
   */
  static Error createUser();

  /**
   * Get reading progress for a document.
   * @param documentHash The document hash (from KOReaderDocumentId)
   * @param outProgress Output: the progress data
   * @return OK on success, NOT_FOUND if no progress exists, error code on failure
   */
  static Error getProgress(const std::string& documentHash, KOReaderProgress& outProgress);

  /**
   * Update reading progress for a document.
   * @param progress The progress data to upload
   * @return OK on success, error code on failure
   */
  static Error updateProgress(const KOReaderProgress& progress);

  /**
   * Get human-readable error message.
   */
  static const char* errorString(Error error);

  /**
   * Open a connection to `url` the way every KOSync request does: the URL parses, the clock is real before a
   * certificate is judged, https is verified against the roots network_trust gives the host, plain http is
   * taken as typed. Clipping sync opens its connections through this too (D9). False on a bad URL or no clock.
   */
  static bool openConnection(freeink::SecureHttpClient& http, const std::string& url);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;

  /** Per-request timeout in ms for getProgress and updateProgress; 0 keeps the HTTP client's own default. */
  static uint32_t requestTimeoutMs;

  /** Absolute millis deadline for getProgress; 0 keeps the ordinary buffered request path. */
  static uint32_t requestDeadlineMs;
};
