#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace fontdownload {

// Limits e-ink progress refreshes while a network transfer continues to poll
// input at its native cadence. Call every member while holding RenderLock.
class ProgressRenderGate {
 public:
  static constexpr unsigned long MIN_INTERVAL_MS = 2000;
  static constexpr unsigned long MIN_PAINT_INTERVAL_MS = 1000;
  static constexpr int MIN_PERCENT_STEP = 10;

  void reset() {
    lastRequestedBytes_ = std::numeric_limits<size_t>::max();
    lastRequestedPercent_ = -1;
    lastRequestedAt_ = 0;
    renderQueued_ = false;
  }

  bool requestDue(const size_t downloaded, const size_t total, const unsigned long now) {
    if (renderQueued_ || (lastRequestedPercent_ >= 0 && now - lastRequestedAt_ < MIN_PAINT_INTERVAL_MS)) return false;

    const int percent = total == 0 ? 0 : static_cast<int>((static_cast<uint64_t>(downloaded) * 100) / total);
    if (downloaded == lastRequestedBytes_ && percent == lastRequestedPercent_) return false;

    if (lastRequestedPercent_ < 0 || percent >= 100 || percent >= lastRequestedPercent_ + MIN_PERCENT_STEP ||
        now - lastRequestedAt_ >= MIN_INTERVAL_MS) {
      lastRequestedBytes_ = downloaded;
      lastRequestedPercent_ = percent;
      lastRequestedAt_ = now;
      renderQueued_ = true;
      return true;
    }
    return false;
  }

  void renderStarted() { renderQueued_ = false; }
  void paintStarted(const unsigned long now) {
    lastPaintedAt_ = now;
    hasPainted_ = true;
    renderStarted();
  }
  unsigned long paintDelay(const unsigned long now) const {
    const auto elapsed = now - lastPaintedAt_;
    return hasPainted_ && elapsed < MIN_PAINT_INTERVAL_MS ? MIN_PAINT_INTERVAL_MS - elapsed : 0;
  }
  bool renderQueued() const { return renderQueued_; }
  unsigned long nextPaintDelay(const unsigned long now) const {
    const auto elapsed = now - lastRequestedAt_;
    return lastRequestedPercent_ >= 0 && elapsed < MIN_PAINT_INTERVAL_MS ? MIN_PAINT_INTERVAL_MS - elapsed : 0;
  }

 private:
  size_t lastRequestedBytes_ = std::numeric_limits<size_t>::max();
  int lastRequestedPercent_ = -1;
  unsigned long lastRequestedAt_ = 0;
  bool renderQueued_ = false;
  unsigned long lastPaintedAt_ = 0;
  bool hasPainted_ = false;
};

// Rows of the transfer screen (family name, size line, two bars, total line, byte line). Every bar writes its own
// percent label gap pixels below itself, so a row placed under a bar has to start under that label too.
struct TransferLayout {
  int name, status, bar1, total, bar2, size;
};

inline TransferLayout transferLayout(const int centerY, const int lineHeight, const int spacing, const int barHeight,
                                     const int labelGap) {
  const int step = lineHeight + spacing;
  const int barStep = barHeight + labelGap + step;  // bar, its percent label, then the next row
  const int blockHeight = 6 * lineHeight + 5 * spacing + 2 * (barHeight + labelGap);
  TransferLayout layout;
  layout.name = centerY + lineHeight / 2 - blockHeight / 2;
  layout.status = layout.name + step;
  layout.bar1 = layout.status + step;
  layout.total = layout.bar1 + barStep;
  layout.bar2 = layout.total + step;
  layout.size = layout.bar2 + barStep;
  return layout;
}

}  // namespace fontdownload
