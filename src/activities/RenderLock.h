#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  // Non-blocking acquisition: isLocked reflects whether the mutex was won.
  struct TryTake {};
  explicit RenderLock();
  explicit RenderLock(TryTake);
  explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  bool acquired() const { return isLocked; }
  void unlock();
  static bool peek();
};
