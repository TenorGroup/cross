#pragma once

#include <expat.h>

#include <cstddef>
#include <cstdint>

namespace booksearch {
constexpr size_t kMaxResults = 200;
constexpr size_t kMaxQueryCodepoints = 128;
constexpr size_t kExcerptCodepoints = 256;
constexpr size_t kHistoryCodepoints = 512;

enum class Status : uint8_t { Ready, QueryTooLong, NoMemory, ParseError };

struct Result {
  uint16_t spine = 0;
  uint32_t visibleOffset = 0;
  // Highlight range in Unicode codepoints of excerpt, with its original accents.
  uint16_t matchStart = 0;
  uint16_t matchLength = 0;
  bool excerptTruncated = false;
  char excerpt[kExcerptCodepoints * 4 + 1]{};
};

struct Results {
  size_t count = 0;
  Result items[kMaxResults]{};
  bool full() const { return count == kMaxResults; }
  void clear() { count = 0; }
};

struct Workspace {
  struct Character { uint32_t cp; uint32_t offset; };
  struct Tail { size_t bytes; uint16_t chars; uint16_t after; bool done; };
  uint32_t query[kMaxQueryCodepoints]{};
  uint16_t fallback[kMaxQueryCodepoints]{};
  uint32_t starts[kMaxQueryCodepoints]{};
  Character history[kHistoryCodepoints]{};
  Tail tails[kMaxResults]{};
};

class Scanner {
 public:
  Scanner(Workspace& work, Results& results, const XML_Memory_Handling_Suite* memory = nullptr);
  ~Scanner();
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;
  // Keeps prior chapters' results; clear Results explicitly for a new search.
  Status begin(const char* query, uint16_t spine);
  bool feed(const char* bytes, size_t length);
  bool finish();
  uint32_t visibleOffset() const { return offset_; }
  Status status() const { return status_; }

 private:
  static void XMLCALL start(void* data, const char* tag, const char** attrs);
  static void XMLCALL end(void* data, const char* tag);
  static void XMLCALL text(void* data, const char* bytes, int length);
  static void XMLCALL entity(void* data, const char* bytes, int length);
  void character(uint32_t cp);
  void hit(uint32_t first);
  void append(size_t index, uint32_t cp, bool matched);
  void destroy();
  const Workspace::Character& history(size_t index) const;

  Workspace& work_;
  Results& results_;
  const XML_Memory_Handling_Suite* memory_;
  XML_Parser parser_ = nullptr;
  Status status_ = Status::Ready;
  size_t querySize_ = 0;
  size_t matched_ = 0;
  size_t folded_ = 0;
  size_t historyCount_ = 0;
  size_t historyNext_ = 0;
  size_t chapterFirst_ = 0;
  size_t tailFirst_ = 0;
  uint32_t offset_ = 0;
  uint16_t hiddenDepth_ = 0;
  uint16_t depth_ = 0;
  uint16_t hiddenAttributeUntilDepth_ = 0;
  uint16_t spine_ = 0;
  bool insideBody_ = false;
  bool lastSpace_ = false;
};
}  // namespace booksearch
