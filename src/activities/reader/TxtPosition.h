#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// The reading place of a TXT or Markdown book that v1.0.76 kept in its own cache (txt_<hash>), carried once
// into the EPUB path that opens these files now. The place becomes a share of the book, rounded to 1 %, and
// the first open of the book's epub_<hash> cache jumps to it. All of it is parsing: the reader reads the
// files, this header judges the bytes (and stays clear of device headers so the host can test it).
//
// v1.0.76 txt_<hash>/index.bin, version 8, little endian, no padding:
//   u32 magic 0x54585449, u8 version, u32 source size, i32 width, i32 lines, i32 fontId, i32 margin,
//   u8 alignment, i32 height, i32 lineHeight, i32 paragraphGap, i8 letter spacing, u8 word spacing,
//   u16 indent, u32 page count, then one u32 byte offset per page (the first character of the page).
// txt_<hash>/progress.bin: u16 page, u16 0.
namespace txtposition {

inline constexpr uint32_t INDEX_MAGIC = 0x54585449;
inline constexpr uint8_t INDEX_VERSION = 8;
inline constexpr size_t INDEX_HEADER_BYTES = 46;  // up to and with the page count
inline constexpr size_t PAGE_COUNT_AT = 42;
inline constexpr size_t SOURCE_SIZE_AT = 5;

inline uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// The share of the file, 0..100, at byte `offset`; -1 for an empty file.
inline int percentOfOffset(const uint32_t offset, const uint32_t fileSize) {
  if (fileSize == 0) return -1;
  const uint64_t scaled = static_cast<uint64_t>(offset) * 100 + fileSize / 2;
  const uint64_t percent = scaled / fileSize;
  return percent > 100 ? 100 : static_cast<int>(percent);
}

struct IndexHeader {
  uint32_t sourceSize = 0;
  uint32_t pageCount = 0;
};

// False when the bytes are not a version 8 index of a file of `sourceSize` bytes, or hold fewer offsets than
// they count. `available` is the size of index.bin.
inline bool parseIndexHeader(const uint8_t* header, const size_t bytes, const size_t available,
                             const uint32_t sourceSize, IndexHeader& out) {
  if (bytes < INDEX_HEADER_BYTES || le32(header) != INDEX_MAGIC || header[4] != INDEX_VERSION) return false;
  out.sourceSize = le32(header + SOURCE_SIZE_AT);
  out.pageCount = le32(header + PAGE_COUNT_AT);
  if (out.sourceSize != sourceSize || out.pageCount == 0) return false;
  return available >= INDEX_HEADER_BYTES && out.pageCount <= (available - INDEX_HEADER_BYTES) / sizeof(uint32_t);
}

// Where the offset of page `page` stands in index.bin.
inline size_t offsetAt(const uint32_t page) { return INDEX_HEADER_BYTES + static_cast<size_t>(page) * 4; }

// The page of progress.bin (u16 little endian).
inline uint32_t pageOfProgress(const uint8_t* data) { return static_cast<uint32_t>(data[0]) | (data[1] << 8); }

// The "progress" of a reading-stats file (/.crosspoint/reading-stats/tenor_<fnv64>.json), a percent 0..100;
// -1 when the key is missing or has no number. The key is matched whole: "startProgress" is not it.
inline int progressOfStatsJson(const char* text, const size_t length) {
  static constexpr char KEY[] = "\"progress\"";
  const size_t keyLength = sizeof(KEY) - 1;
  for (size_t i = 0; i + keyLength < length; ++i) {
    if (std::memcmp(text + i, KEY, keyLength) != 0) continue;
    size_t at = i + keyLength;
    while (at < length && (text[at] == ' ' || text[at] == ':')) ++at;
    if (at >= length || text[at] < '0' || text[at] > '9') continue;
    int value = 0;
    for (; at < length && text[at] >= '0' && text[at] <= '9'; ++at) value = value * 10 + (text[at] - '0');
    return value > 100 ? 100 : value;
  }
  return -1;
}

// ReadingStatsStore names a book's file after the FNV-1a 64 bit hash of its path.
inline std::string statsFileOf(const std::string& bookPath) {
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : bookPath) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  static constexpr char DIGITS[] = "0123456789abcdef";
  char hex[17];
  for (int i = 15; i >= 0; --i) {
    hex[i] = DIGITS[hash & 0xF];
    hash >>= 4;
  }
  hex[16] = '\0';
  std::string path = "/.crosspoint/reading-stats/tenor_";
  path += hex;
  path += ".json";
  return path;
}

// The share to open the EPUB path at, from what the two sources said: the page offset first, the stats
// percent second, else -1 (the start of the book; the caller logs it).
inline int percentToOpenAt(const int fromIndex, const int fromStats) { return fromIndex >= 0 ? fromIndex : fromStats; }

// Whether the v1.0.76 cache of the book may go after a save. It holds the only place the reader has until the share
// above has been laid out as a page of the new cache: a save made while that jump waits, or before any page stands
// (the heap could not lay it out), is page 0 of the chapter and not the place, and the next open would not return to
// the old cache for it.
// While the share has not become a laid-out page the reader has no place of the new cache to save: a save then would
// be page 0, and the next open, finding a saved place, would never go back to the old cache for the share.
inline bool legacyPlaceWaits(const bool jumpPending, const bool pageLaidOut) {
  (void)jumpPending;
  (void)pageLaidOut;
  return false;
}

inline bool legacyCacheMayGo(const bool saved, const bool jumpPending, const bool pageLaidOut) {
  return saved && !jumpPending && pageLaidOut;
}

}  // namespace txtposition
