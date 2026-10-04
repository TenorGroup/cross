#pragma once

#include <string>

namespace thumbname {

// Recent entries saved before the card-shaped thumbnails still name the old stretched EPUB
// files; point them at the new name so Home builds the new thumbnail instead of drawing the old
// one. Only EPUB caches moved to the new name: other book types still write thumb_.
inline void moveOldEpubThumb(std::string& path) {
  const char* old = "/thumb_[HEIGHT].bmp";
  const size_t at = path.rfind(old);
  if (at != std::string::npos && at + std::char_traits<char>::length(old) == path.size() &&
      path.rfind("/epub_", at) != std::string::npos) {
    path.replace(at, std::string::npos, "/thumb2_[HEIGHT].bmp");
  }
}

}  // namespace thumbname
