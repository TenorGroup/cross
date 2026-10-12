#include "FileBrowserSort.h"

#include <FsHelpers.h>

namespace filebrowser {
void sortNames(std::vector<std::string>& names, uint64_t* keys, const SortMode mode) {
  if (mode == SortMode::Name || !keys) {
    FsHelpers::sortFileList(names);
    return;
  }
  const auto before = [&](size_t a, size_t b) {
    const bool dirA = !names[a].empty() && names[a].back() == '/';
    const bool dirB = !names[b].empty() && names[b].back() == '/';
    if (dirA != dirB) return dirA;
    if (!dirA && keys[a] != keys[b]) return keys[a] > keys[b];
    return FsHelpers::naturalLess(names[a], names[b]);
  };
  const auto swap = [&](size_t a, size_t b) {
    names[a].swap(names[b]);
    std::swap(keys[a], keys[b]);
  };
  const auto sift = [&](size_t parent, size_t count) {
    while (parent < count / 2) {
      size_t child = parent * 2 + 1;
      if (child + 1 < count && before(child, child + 1)) ++child;
      if (!before(parent, child)) break;
      swap(parent, child);
      parent = child;
    }
  };
  for (size_t n = names.size() / 2; n; --n) sift(n - 1, names.size());
  for (size_t n = names.size(); n > 1; --n) {
    swap(0, n - 1);
    sift(0, n - 1);
  }
}
}  // namespace filebrowser
