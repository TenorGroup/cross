#pragma once

#include <mutex>

// Socket of the HTTP upload being read, shared by the main task, which reads the upload, and
// the Back sampler task, which shuts the socket's read side so a stalled upload ends at once.
// The main task retracts the socket before the server closes it; one lock makes "still the
// upload's socket" and the shut a single step, so a number the closed socket handed on to
// another connection is never shut.
class UploadSocket {
 public:
  void note(const int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    fd_ = fd;
  }
  void retract() {
    std::lock_guard<std::mutex> lock(mutex_);
    fd_ = -1;
  }
  // Shuts the upload's socket with `shut(fd)`; false when no upload is being read.
  template <typename Shut>
  bool interrupt(Shut shut) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ < 0) return false;
    shut(fd_);
    return true;
  }

 private:
  std::mutex mutex_;
  int fd_ = -1;
};
