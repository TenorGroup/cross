#pragma once

#include <cstdint>

namespace filesearch {

inline constexpr int SEARCH_HEIGHT = 60;
inline constexpr int SEARCH_GAP = 8;

struct Drag {
  int startX = 0;
  int startY = 0;
  int deltaY = 0;
  unsigned long heldMs = 0;
  uint8_t contacts = 1;
  bool insideList = false;
  bool blocked = false;
};

class Reveal {
 private:
  bool shown_ = false;

 public:
  bool visible(bool browser, bool hasQuery) const { return browser && (shown_ || hasQuery); }

  bool update(const Drag& drag, bool browser, bool atTop, bool hasQuery) {
    const bool wasVisible = visible(browser, hasQuery);
    const bool validOrigin = browser && drag.contacts == 1 && drag.insideList && !drag.blocked && drag.startY > 112;
    if (validOrigin && !hasQuery && drag.deltaY < 0) {
      shown_ = false;
    } else if (validOrigin && atTop && drag.deltaY >= 60 && drag.heldMs > 300) {
      shown_ = true;
    }
    return wasVisible != visible(browser, hasQuery);
  }

  void reset() { shown_ = false; }
};

}  // namespace filesearch
