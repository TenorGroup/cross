#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  // Two spellings of the non-blocking acquisition: RenderLock(TryTake{}) + acquired() and
  // RenderLock(Mode::Try) + ownsLock(). Both share one implementation; isLocked reflects whether
  // the mutex was won. The default constructor blocks.
  enum class Mode { Blocking, Try };
  struct TryTake {};
  explicit RenderLock();
  explicit RenderLock(Mode mode);
  explicit RenderLock(TryTake);
  explicit RenderLock(Activity&);  // Activity argument retained for compatibility.
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  bool acquired() const { return isLocked; }
  bool ownsLock() const { return isLocked; }
  void unlock();
  static bool peek();
};
