#include "BookSearch.h"

#if !defined(ARDUINO) || defined(BOARD_HAS_PSRAM)
#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace booksearch {
namespace {
bool space(uint32_t cp) {
  return cp == ' ' || (cp >= 9 && cp <= 13) || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
         cp == 0x205F || cp == 0x3000;
}

uint32_t fold(uint32_t cp) {
  if (utf8IsCombiningMark(cp)) return 0;
  if (space(cp)) return ' ';
  while (const uint32_t base = utf8DecomposedBase(cp)) cp = base;
  if (cp == 0x110 || cp == 0x111) return 'd';
  if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
  if ((cp >= 0xC0 && cp <= 0xD6) || (cp >= 0xD8 && cp <= 0xDE) || (cp >= 0x391 && cp <= 0x3A1) ||
      (cp >= 0x3A3 && cp <= 0x3AB) || (cp >= 0x410 && cp <= 0x42F)) return cp + 32;
  if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
  return cp;
}

size_t encode(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 63));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 63));
    out[2] = static_cast<char>(0x80 | (cp & 63));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 63));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 63));
  out[3] = static_cast<char>(0x80 | (cp & 63));
  return 4;
}

}  // namespace

Scanner::Scanner(Workspace& work, Results& results, const XML_Memory_Handling_Suite* memory)
    : work_(work), results_(results), memory_(memory) {}
Scanner::~Scanner() { destroy(); }
void Scanner::destroy() {
  if (parser_) XML_ParserFree(parser_);
  parser_ = nullptr;
}

Status Scanner::begin(const char* query, const uint16_t spine) {
  destroy();
  status_ = Status::Ready;
  querySize_ = matched_ = folded_ = historyCount_ = historyNext_ = 0;
  offset_ = hiddenDepth_ = depth_ = hiddenAttributeUntilDepth_ = 0;
  insideBody_ = lastSpace_ = false;
  chapterFirst_ = results_.count;
  tailFirst_ = chapterFirst_;
  spine_ = spine;
  const auto* cp = reinterpret_cast<const unsigned char*>(query ? query : "");
  while (*cp) {
    const uint32_t value = fold(utf8NextCodepoint(&cp));
    if (!value || (value == ' ' && (querySize_ == 0 || work_.query[querySize_ - 1] == ' '))) continue;
    if (querySize_ == kMaxQueryCodepoints) return status_ = Status::QueryTooLong;
    work_.query[querySize_++] = value;
  }
  if (querySize_ && work_.query[querySize_ - 1] == ' ') --querySize_;
  if (!querySize_ || results_.full()) return status_;
  work_.fallback[0] = 0;
  for (size_t i = 1, n = 0; i < querySize_; ++i) {
    while (n && work_.query[i] != work_.query[n]) n = work_.fallback[n - 1];
    if (work_.query[i] == work_.query[n]) ++n;
    work_.fallback[i] = static_cast<uint16_t>(n);
  }
  parser_ = XML_ParserCreate_MM(nullptr, memory_, nullptr);
  if (!parser_) return status_ = Status::NoMemory;
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, start, end);
  XML_SetCharacterDataHandler(parser_, text);
  XML_SetDefaultHandlerExpand(parser_, entity);
  return status_;
}

bool Scanner::feed(const char* bytes, const size_t length) {
  if (status_ != Status::Ready) return false;
  if (!querySize_ || results_.full()) return true;
  if (!parser_ || (!bytes && length) || length > INT_MAX) return false;
  if (XML_Parse(parser_, bytes, static_cast<int>(length), XML_FALSE) == XML_STATUS_OK) return true;
  if (results_.full() && XML_GetErrorCode(parser_) == XML_ERROR_ABORTED) return true;
  status_ = XML_GetErrorCode(parser_) == XML_ERROR_NO_MEMORY ? Status::NoMemory : Status::ParseError;
  return false;
}

bool Scanner::finish() {
  bool ok = status_ == Status::Ready;
  if (ok && parser_ && !results_.full() && XML_Parse(parser_, "", 0, XML_TRUE) != XML_STATUS_OK) {
    status_ = XML_GetErrorCode(parser_) == XML_ERROR_NO_MEMORY ? Status::NoMemory : Status::ParseError;
    ok = false;
  }
  for (size_t i = chapterFirst_; i < results_.count; ++i) work_.tails[i].done = true;
  destroy();
  return ok;
}

void XMLCALL Scanner::start(void* data, const char* tag, const char** attrs) {
  auto& self = *static_cast<Scanner*>(data);
  ++self.depth_;
  if (VisibleTextUtils::equalsTag(tag, "body")) self.insideBody_ = true;
  if (self.insideBody_ && (self.hiddenDepth_ || VisibleTextUtils::isNonVisibleElement(tag))) ++self.hiddenDepth_;
  if (self.insideBody_ && attrs) {
    for (int i = 0; attrs[i]; i += 2) {
      if (VisibleTextUtils::equalsTag(attrs[i], "hidden")) {
        self.hiddenAttributeUntilDepth_ = self.depth_;
        break;
      }
    }
  }
}
void XMLCALL Scanner::end(void* data, const char* tag) {
  auto& self = *static_cast<Scanner*>(data);
  if (self.hiddenDepth_) --self.hiddenDepth_;
  if (self.depth_ == self.hiddenAttributeUntilDepth_) self.hiddenAttributeUntilDepth_ = 0;
  if (self.depth_) --self.depth_;
  if (std::strcmp(tag, "body") == 0) self.insideBody_ = false;
}
void XMLCALL Scanner::text(void* data, const char* bytes, const int length) {
  auto& self = *static_cast<Scanner*>(data);
  if (!VisibleTextUtils::countsSourceCharacters(self.insideBody_, self.hiddenDepth_, false)) return;
  const auto* cp = reinterpret_cast<const unsigned char*>(bytes);
  const auto* end = cp + length;
  if (self.hiddenAttributeUntilDepth_) {
    while (cp < end) {
      utf8NextCodepoint(&cp);
      ++self.offset_;
    }
    return;
  }
  while (cp < end && !self.results_.full()) self.character(utf8NextCodepoint(&cp));
  if (self.results_.full()) XML_StopParser(self.parser_, XML_FALSE);
}
void XMLCALL Scanner::entity(void* data, const char* bytes, const int length) {
  if (length < 3 || bytes[0] != '&' || bytes[length - 1] != ';') return;
  const char* expanded = lookupHtmlEntity(bytes, static_cast<size_t>(length));
  text(data, expanded ? expanded : bytes, expanded ? static_cast<int>(std::strlen(expanded)) : length);
}

const Workspace::Character& Scanner::history(const size_t index) const {
  return work_.history[(historyNext_ + kHistoryCodepoints - historyCount_ + index) % kHistoryCodepoints];
}

void Scanner::append(const size_t index, const uint32_t cp, const bool matched) {
  auto& tail = work_.tails[index];
  if (tail.done) return;
  auto& result = results_.items[index];
  if (tail.chars == kExcerptCodepoints) {
    result.excerptTruncated = true;
    tail.done = true;
    return;
  }
  char encoded[4];
  const size_t encodedBytes = encode(cp, encoded);
  std::memcpy(result.excerpt + tail.bytes, encoded, encodedBytes);
  tail.bytes += encodedBytes;
  result.excerpt[tail.bytes] = '\0';
  ++tail.chars;
  if (matched) ++result.matchLength;
}

void Scanner::hit(const uint32_t first) {
  const size_t index = results_.count++;
  auto& result = results_.items[index];
  result = Result{};
  result.spine = spine_;
  result.visibleOffset = first;
  work_.tails[index] = {};
  size_t match = 0;
  while (match < historyCount_ && history(match).offset < first) ++match;
  result.excerptTruncated = match == 0 && historyCount_ && history(0).offset > first;
  size_t begin = match > 25 ? match - 25 : 0;
  if (begin && !space(history(begin - 1).cp)) {
    size_t boundary = begin;
    while (boundary < match && !space(history(boundary).cp)) ++boundary;
    if (boundary < match) begin = boundary + 1;
    else {
      while (begin && !space(history(begin - 1).cp) && match - begin < 128) --begin;
      if (begin && !space(history(begin - 1).cp)) result.excerptTruncated = true;
    }
  }
  while (begin < match && space(history(begin).cp)) ++begin;
  result.matchStart = static_cast<uint16_t>(match - begin);
  for (size_t i = begin; i < historyCount_; ++i) append(index, history(i).cp, i >= match);
}

void Scanner::character(const uint32_t cp) {
  const uint32_t original = offset_++;
  const uint32_t folded = fold(cp);
  const bool isSpace = space(cp);
  const bool duplicateSpace = isSpace && lastSpace_;
  if (!duplicateSpace) {
    const uint32_t display = isSpace ? ' ' : cp;
    while (tailFirst_ < results_.count && work_.tails[tailFirst_].done) ++tailFirst_;
    for (size_t i = tailFirst_; i < results_.count; ++i) {
      auto& tail = work_.tails[i];
      if (tail.done) continue;
      if (isSpace && tail.after >= 25) { tail.done = true; continue; }
      const bool markAtEnd = !tail.after && utf8IsCombiningMark(cp);
      append(i, display, markAtEnd);
      if (!markAtEnd) ++tail.after;
    }
    work_.history[historyNext_] = {display, original};
    historyNext_ = (historyNext_ + 1) % kHistoryCodepoints;
    historyCount_ = std::min(historyCount_ + 1, kHistoryCodepoints);
  }
  if (isSpace) lastSpace_ = true;
  else if (!utf8IsCombiningMark(cp)) lastSpace_ = false;
  if (!folded || duplicateSpace) return;
  work_.starts[folded_ % kMaxQueryCodepoints] = original;
  ++folded_;
  while (matched_ && folded != work_.query[matched_]) matched_ = work_.fallback[matched_ - 1];
  if (folded == work_.query[matched_]) ++matched_;
  if (matched_ == querySize_) {
    hit(work_.starts[(folded_ - querySize_) % kMaxQueryCodepoints]);
    matched_ = work_.fallback[matched_ - 1];
  }
}
}  // namespace booksearch
#endif
