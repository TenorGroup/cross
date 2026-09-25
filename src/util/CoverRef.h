#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// <book cache>/cover.ref: where a book's cover sits inside its zip. The reader writes it after the
// book's first frame while the book's thumbnails are still owed, so Home writes them without
// loading the book's index (book.bin: 1,3 s for 5.000 chapters on the X3, before the cover decode).
// An empty path is a book without a cover. The file is numbered: another version, a cut write or a
// damaged file reads as missing, and Home loads the book as before.
namespace coverref {

constexpr uint8_t MAGIC[4] = {'C', 'R', 'E', 'F'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEAD = 6;  // magic, version, path length
constexpr size_t MAX_BYTES = HEAD + 255;

inline std::string path(const std::string& cachePath) { return cachePath + "/cover.ref"; }

// The file's bytes for this cover path; 0 when it does not fit in `room` or in one length byte.
inline size_t encode(const std::string& href, uint8_t* out, const size_t room) {
  const size_t size = HEAD + href.size();
  if (href.size() > 255 || size > room) return 0;
  std::memcpy(out, MAGIC, sizeof(MAGIC));
  out[4] = VERSION;
  out[5] = static_cast<uint8_t>(href.size());
  std::memcpy(out + HEAD, href.data(), href.size());
  return size;
}

// True when `data` is a whole file of this version; `href` then holds the cover path.
inline bool decode(const uint8_t* data, const size_t size, std::string& href) {
  if (!data || size < HEAD || std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0 || data[4] != VERSION ||
      size != HEAD + data[5])
    return false;
  href.assign(reinterpret_cast<const char*>(data + HEAD), data[5]);
  return true;
}

// The file itself (CoverRef.cpp): written under ".tmp" and renamed once whole.
bool save(const std::string& cachePath, const std::string& href);
bool load(const std::string& cachePath, std::string& href);

}  // namespace coverref
