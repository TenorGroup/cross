#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

enum class WebDavPullStatus {
  wifi_selection,
  needs_configuration,
  ready,
  discovering,
  downloading,
  complete,
  failed,
};

namespace webdav {

struct WebDavPullLimits {
  static constexpr std::size_t maxHref = 1024;
  static constexpr std::size_t maxProperty = 2048;
  static constexpr std::size_t maxXmlText = 4096;
  static constexpr std::size_t maxPath = 222;
  static constexpr std::size_t maxDepth = 8;
  static constexpr std::size_t maxEntry = 1536;
  static constexpr std::size_t maxQueueBytes = 128 * 1024;
};

enum class ParseError {
  none,
  malformed_xml,
  oversized_xml,
  oversized_href,
  oversized_property,
  oversized_entry,
  invalid_path,
  too_deep,
  unsafe_origin,
};

struct PropfindEntry {
  std::string href;
  std::string path;
  std::string contentType;
  std::uint64_t contentLength = 0;
  std::size_t depth = 0;
  bool collection = false;
};

struct ParseResult {
  std::vector<PropfindEntry> entries;
  ParseError error = ParseError::none;

  bool ok() const { return error == ParseError::none; }
};

// Parse one bounded HTTP 207 Multi-Status body. The optional origin is used to
// validate absolute hrefs. Relative hrefs are always resolved as device paths.
ParseResult parsePropfind207(std::string_view xml, std::string_view expectedOrigin = {});
ParseResult parsePropfind(std::string_view xml, std::string_view expectedOrigin = {});

using PropfindEntrySink = bool (*)(const PropfindEntry& entry, void* context);

// Incremental PROPFIND parser. It retains only the current bounded response
// and invokes the sink as soon as one entry is complete.
class PropfindStreamParser {
 public:
  explicit PropfindStreamParser(std::string_view expectedOrigin = {});

  bool feed(const std::uint8_t* data, std::size_t size, PropfindEntrySink sink, void* context);
  bool finish();
  ParseError error() const { return error_; }
  std::size_t entries() const { return entries_; }

 private:
  bool consume(PropfindEntrySink sink, void* context);
  std::string expectedOrigin_;
  std::string pending_;
  bool sawRoot_ = false;
  bool sawRootClose_ = false;
  bool inResponse_ = false;
  bool sinkAborted_ = false;
  ParseError error_ = ParseError::none;
  std::size_t entries_ = 0;
};

// A redirect is accepted only when both URLs have the same origin. HTTPS never
// follows a downgrade to HTTP.
bool redirectAllowed(std::string_view currentUrl, std::string_view nextUrl);

// A card path a listed file may be written to: absolute, no empty, "." or ".." segment, no
// backslash or control character. Dots inside a name ("Truyện... tập 1.epub") are fine.
bool validLocalPath(std::string_view path);

// Small OCF check used by the queue fixture. It accepts an uncompressed first
// ZIP entry named mimetype with the required EPUB media type.
bool isValidEpub(std::string_view bytes);

enum class PullStatus { downloaded, skipped_existing, collision, invalid, failed };

struct QueueResult {
  PullStatus status = PullStatus::failed;
  bool queued = false;
};

// The status of one listed file once its transfer is over: skipped when the card already has it,
// otherwise judged on what arrived.
PullStatus downloadStatus(const PropfindEntry& entry, bool destinationExists, bool downloadSucceeded, bool epubValid);

// Whether the pull goes on to the next listed file after one ended with this status.
bool pullContinuesAfter(PullStatus status);

// The screen once the listing has been walked: complete, unless a step failed or was cancelled. A
// listing with nothing to fetch (only folders or TXT) is complete too.
WebDavPullStatus statusAfterPull(WebDavPullStatus current);

class PullQueue {
 public:
  QueueResult submit(const PropfindEntry& entry, bool destinationExists, bool downloadSucceeded,
                     bool epubValid = true);

  std::size_t bytes() const { return bytes_; }
  std::size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const std::deque<PropfindEntry>& items() const { return items_; }

 private:
  static std::size_t itemBytes(const PropfindEntry& entry);

  std::deque<PropfindEntry> items_;
  std::vector<std::string> paths_;
  std::size_t bytes_ = 0;
};

}  // namespace webdav
