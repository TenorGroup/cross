#include "FileSearch.h"

#include <LibraryText.h>

#include <algorithm>
#include <string_view>

namespace filesearch {
namespace {

bool naturalLess(const std::string_view left, const std::string_view right) {
  size_t a = 0, b = 0;
  while (a < left.size() && b < right.size()) {
    if (left[a] >= '0' && left[a] <= '9' && right[b] >= '0' && right[b] <= '9') {
      size_t ae = a, be = b;
      while (ae < left.size() && left[ae] >= '0' && left[ae] <= '9') ++ae;
      while (be < right.size() && right[be] >= '0' && right[be] <= '9') ++be;
      size_t az = a;
      while (az + 1 < ae && left[az] == '0') ++az;
      size_t bz = b;
      while (bz + 1 < be && right[bz] == '0') ++bz;
      const auto alen = ae - az, blen = be - bz;
      if (alen != blen) return alen < blen;
      if (const int cmp = left.substr(az, alen).compare(right.substr(bz, blen)); cmp != 0) return cmp < 0;
      if (ae - a != be - b) return ae - a < be - b;
      a = ae;
      b = be;
      continue;
    }
    if (left[a] != right[b]) return left[a] < right[b];
    ++a;
    ++b;
  }
  return left.size() < right.size();
}

std::string stem(const std::string& entry) {
  if (entry.empty()) return {};
  std::string value = entry;
  if (!value.empty() && value.back() == '/') value.pop_back();
  if (const size_t slash = value.find_last_of('/'); slash != std::string::npos) value.erase(0, slash + 1);
  if (const size_t dot = value.rfind('.'); dot != std::string::npos) value.resize(dot);
  return value;
}

bool isBreak(const char c) { return c == ' ' || c == '\''; }

bool everyWord(const std::string& haystack, const std::string& needle, const bool anywhere) {
  size_t qs = 0;
  while (qs < needle.size()) {
    while (qs < needle.size() && isBreak(needle[qs])) ++qs;
    if (qs >= needle.size()) break;
    size_t qe = qs;
    while (qe < needle.size() && !isBreak(needle[qe])) ++qe;
    const std::string_view word(needle.data() + qs, qe - qs);
    bool found = false;
    size_t hs = 0;
    while (hs < haystack.size()) {
      while (hs < haystack.size() && isBreak(haystack[hs])) ++hs;
      if (hs >= haystack.size()) break;
      const bool atWordStart = hs == 0 || isBreak(haystack[hs - 1]);
      if ((anywhere || atWordStart) && haystack.compare(hs, word.size(), word) == 0) {
        found = true;
        break;
      }
      ++hs;
    }
    if (!found) return false;
    qs = qe;
  }
  return true;
}

}  // namespace

bool IndexBuffer::allocate(const size_t capacity, const char* owner) {
  data_.reset();
  capacity_ = size_ = 0;
  if (capacity == 0) return true;
  if (capacity > UINT16_MAX) return false;
  auto data = heapfloor::array<uint16_t[]>(capacity, owner);
  if (!data) return false;
  data_ = std::move(data);
  capacity_ = capacity;
  return true;
}

uint8_t matchRank(const std::string& entry, const std::string& query) {
  const std::string needle = library::fold(query);
  if (needle.empty()) return 0;
  const std::string haystack = library::fold(stem(entry));
  if (haystack.empty()) return 255;
  if (haystack.compare(0, needle.size(), needle) == 0) return 0;
  if (everyWord(haystack, needle, false)) return 1;
  if (everyWord(haystack, needle, true)) return 2;
  return 255;
}

bool buildResults(const std::vector<std::string>& source, const std::string& query, IndexBuffer& result,
                  const char* owner) {
  if (!result.allocate(source.size(), owner)) return false;
  for (size_t i = 0; i < source.size(); ++i) {
    if (matchRank(source[i], query) != 255 && !result.push(static_cast<uint16_t>(i))) return false;
  }
  std::sort(result.data(), result.data() + result.size(), [&](const uint16_t left, const uint16_t right) {
    const uint8_t leftRank = matchRank(source[left], query);
    const uint8_t rightRank = matchRank(source[right], query);
    if (leftRank != rightRank) return leftRank < rightRank;
    if (naturalLess(source[left], source[right])) return true;
    if (naturalLess(source[right], source[left])) return false;
    return source[left] < source[right];
  });
  return true;
}

}  // namespace filesearch
