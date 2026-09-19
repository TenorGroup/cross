#pragma once

namespace ble_runtime_test {
inline bool renderLockHeld = false;
inline unsigned renderLockAcquires = 0;
}  // namespace ble_runtime_test

class RenderLock {
 public:
  RenderLock() {
    renderLockHeld = true;
    ++ble_runtime_test::renderLockAcquires;
  }
  explicit RenderLock(void*) : RenderLock() {}
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock() { renderLockHeld = false; }

  void unlock() { renderLockHeld = false; }
  static bool peek() { return renderLockHeld; }

 private:
  static inline bool& renderLockHeld = ble_runtime_test::renderLockHeld;
};
