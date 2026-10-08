#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace readerstatus {

// Session-only pace. Durations start at the painted page and end at an accepted
// forward turn; ordinary footer repaints leave that start alone.
class Pace {
 public:
  struct Position {
    int chapter = 0, page = 0, pages = 0;
    float bookFraction = -1;
    bool valid() const {
      return chapter >= 0 && page >= 0 && pages > page && std::isfinite(bookFraction) &&
             bookFraction >= 0 && bookFraction <= 1;
    }
  };
  static constexpr uint32_t MIN_SAMPLE_MS = 2000;
  static constexpr uint32_t MAX_SAMPLE_MS = 180000;
  static constexpr uint32_t MIN_SAMPLES = 3;

  void shown(uint32_t now, Position position, uint32_t layout) {
    if (layout != layout_) { *this = {}; layout_ = layout; }
    if (!position.valid()) { suspend(); return; }
    if (armed_ && position.chapter == painted_.chapter && position.page == painted_.page) return;
    painted_ = position;
    shownMs_ = now;
    armed_ = true;
  }
  void suspend() { armed_ = false; }

  bool turned(uint32_t now, bool forward, Position next) {
    const bool armed = armed_;
    suspend();
    const uint32_t duration = now - shownMs_;
    const bool adjacent = (next.chapter == painted_.chapter && next.page == painted_.page + 1) ||
                          (next.chapter == painted_.chapter + 1 && next.page == 0 &&
                           painted_.page == painted_.pages - 1);
    const float progress = next.bookFraction - painted_.bookFraction;
    if (!armed || !forward || !next.valid() || !adjacent || duration < MIN_SAMPLE_MS ||
        duration > MAX_SAMPLE_MS || !std::isfinite(progress) || progress <= 0 || count_ == UINT32_MAX) return false;
    totalMs_ += duration;
    totalProgress_ += progress;
    ++count_;
    return true;
  }

  bool estimate(Position current, bool book, uint32_t& seconds) const {
    seconds = 0;
    if (!current.valid() || count_ < MIN_SAMPLES || totalProgress_ <= 0) return false;
    const double remainingMs = book ? totalMs_ * (1.0 - current.bookFraction) / totalProgress_
                                    : static_cast<double>(totalMs_) * (current.pages - current.page) / count_;
    if (!std::isfinite(remainingMs) || remainingMs < 0) return false;
    seconds = static_cast<uint32_t>(std::min(remainingMs / 1000 + 0.5, static_cast<double>(UINT32_MAX)));
    return true;
  }
  uint32_t samples() const { return count_; }

 private:
  Position painted_{};
  uint64_t totalMs_ = 0;
  double totalProgress_ = 0;
  uint32_t shownMs_ = 0, layout_ = 0, count_ = 0;
  bool armed_ = false;
};

}  // namespace readerstatus
