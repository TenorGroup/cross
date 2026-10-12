#include "FileSearch.h"
#include <LibraryText.h>
#include <algorithm>

namespace filesearch {
std::string_view basename(std::string_view path) {
  const bool dir = !path.empty() && path.back() == '/';
  if (dir) path.remove_suffix(1);
  const auto slash = path.rfind('/');
  if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
  return path;
}
std::string_view stem(std::string_view path) {
  const bool dir = !path.empty() && path.back() == '/';
  auto name = basename(path);
  const auto dot = name.rfind('.');
  if (!dir && dot != std::string_view::npos) name = name.substr(0, dot);
  return name;
}
std::string_view parent(std::string_view path) {
  if (!path.empty() && path.back() == '/') path.remove_suffix(1);
  const auto slash = path.rfind('/');
  return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}
namespace {
bool digit(char c) { return c >= '0' && c <= '9'; }
unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
int naturalOrder(std::string_view a, std::string_view b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (digit(a[i]) && digit(b[j])) {
      while (i < a.size() && a[i] == '0') ++i;
      while (j < b.size() && b[j] == '0') ++j;
      size_t ie = i, je = j;
      while (ie < a.size() && digit(a[ie])) ++ie;
      while (je < b.size() && digit(b[je])) ++je;
      if (ie - i != je - j) return ie - i < je - j ? -1 : 1;
      const int cmp = a.substr(i, ie - i).compare(b.substr(j, je - j));
      if (cmp) return cmp;
      i = ie; j = je;
    } else {
      if (lower(a[i]) != lower(b[j])) return lower(a[i]) < lower(b[j]) ? -1 : 1;
      ++i; ++j;
    }
  }
  return i == a.size() ? (j == b.size() ? 0 : -1) : 1;
}
bool wordBreak(char c) { return c == ' ' || c == '\''; }
}
bool nameLess(std::string_view a, std::string_view b) {
  const int order = naturalOrder(library::fold(basename(a)), library::fold(basename(b)));
  return order ? order < 0 : a < b;
}
Query::Query(std::string_view text) : folded_(library::fold(text)) {}
uint8_t Query::rank(std::string_view entry) const {
  if (folded_.empty()) return 0;
  const auto name = library::fold(stem(entry));
  if (name.compare(0, folded_.size(), folded_) == 0) return 0;
  bool allPrefixes = true;
  for (size_t start = 0; start < folded_.size();) {
    while (start < folded_.size() && wordBreak(folded_[start])) ++start;
    size_t end = start;
    while (end < folded_.size() && !wordBreak(folded_[end])) ++end;
    if (start == end) break;
    const std::string_view word(folded_.data() + start, end - start);
    auto at = name.find(word);
    if (at == std::string::npos) return 255;
    while (at != std::string::npos && at != 0 && !wordBreak(name[at - 1])) at = name.find(word, at + 1);
    if (at == std::string::npos) allPrefixes = false;
    start = end;
  }
  return allPrefixes ? 1 : 2;
}
uint8_t matchRank(const std::string& entry, const std::string& query) { return Query(query).rank(entry); }
bool IndexBuffer::allocate(size_t capacity, const char* owner) {
  clear();
  if (!capacity) return true;
  if (capacity > UINT16_MAX) return false;
  data_ = heapfloor::array<uint16_t[]>(capacity, owner);
  if (!data_) return false;
  capacity_ = capacity;
  return true;
}
bool buildResults(const std::vector<std::string>& source, const std::string& text, IndexBuffer& result,
                  const char* owner) {
  if (!result.allocate(source.size(), owner)) return false;
  // Empty queries restore the original directory order, including folders first.
  const Query query(text);
  for (size_t i = 0; i < source.size(); ++i)
    if (query.rank(source[i]) != 255) result.push(static_cast<uint16_t>(i));
  if (!text.empty() && result.size() > 1)
    std::sort(result.data(), result.data() + result.size(), [&](uint16_t a, uint16_t b) {
      const auto ar = query.rank(source[a]), br = query.rank(source[b]);
      return ar != br ? ar < br : nameLess(source[a], source[b]);
    });
  return true;
}
}  // namespace filesearch
