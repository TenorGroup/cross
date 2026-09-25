#pragma once

#include <string>

namespace thumbname {

// Recent entries saved before the card-shaped thumbnails still name the old stretched EPUB
// files; point them at the new name so Home builds the new thumbnail instead of drawing the old
// one. Only EPUB caches moved to the new name: other book types still write thumb_.
inline void moveOldEpubThumb(std::string& path) {
  static constexpr char kOld[] = "/thumb_[HEIGHT].bmp";
  const size_t at = path.rfind(kOld);
  if (at != std::string::npos && at + sizeof(kOld) - 1 == path.size() &&
      path.rfind("/epub_", at) != std::string::npos) {
    path.replace(at, sizeof(kOld) - 1, "/thumb2_[HEIGHT].bmp");
  }
}

}  // namespace thumbname
