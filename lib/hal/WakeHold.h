#pragma once

#include <atomic>
#include <cstdint>

namespace wakehold {

// The power key's hold through a wake, watched while the boot goes on. The X3 takes a power-key
// wake as real only once the key has stayed down for a window (400 ms): a ghost wake lets go
// sooner. A timer calls sample() with the key level; the boot asks early() at any time, and
// final() once the window is up.
//
// One release inside the window is enough to fail the hold and it is never taken back, so a short
// release between two samples of the boot, or a release and a new press, still fails it. A release
// after the window does not count: the boot may take longer than the window to ask for the answer.
class Watch {
 public:
  void start(const bool pressed, const uint32_t nowMs, const uint16_t windowMs) {
    released_.store(!pressed, std::memory_order_relaxed);
    startMs_ = nowMs;
    windowMs_ = windowMs;
  }

  // Timer side, and the boot's own looks.
  void sample(const bool pressed, const uint32_t nowMs) {
    if (!pressed && remainingMs(nowMs) > 0) released_.store(true, std::memory_order_relaxed);
  }

  // Held so far: the answer for a boot step that cannot wait.
  bool early() const { return !released_.load(std::memory_order_relaxed); }

  uint32_t remainingMs(const uint32_t nowMs) const {
    const uint32_t elapsed = nowMs - startMs_;
    return elapsed >= windowMs_ ? 0 : windowMs_ - elapsed;
  }

  // The window is up and the key never let go.
  bool final(const uint32_t nowMs) const { return remainingMs(nowMs) == 0 && early(); }

 private:
  std::atomic<bool> released_{false};
  uint32_t startMs_ = 0;
  uint16_t windowMs_ = 0;
};

}  // namespace wakehold
