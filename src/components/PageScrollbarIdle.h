#pragma once

#include <atomic>
#include <cstdint>

class PageScrollbarIdle {
 public:
  static constexpr uint32_t HIDE_DELAY_MS = 2000;

  void show(const uint32_t now) {
    shownAt.store(now);
    changes.fetch_add(1);
    visible.store(true);
  }
  void hide() { visible.store(false); }
  bool isVisible() const { return visible.load(); }
  uint32_t generation() const { return changes.load(); }
  void hideIfUnchanged(const uint32_t generation) {
    visible.store(false);
    if (changes.load() != generation) visible.store(true);
  }
  bool expired(const uint32_t now) const {
    return visible.load() && static_cast<uint32_t>(now - shownAt.load()) >= HIDE_DELAY_MS;
  }

 private:
  std::atomic<uint32_t> shownAt{0};
  std::atomic<bool> visible{false};
  std::atomic<uint32_t> changes{0};
};
