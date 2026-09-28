#pragma once
#include <cstddef>
#include <cstdint>

// One block kept for wolfSSL's receive buffer during the firmware download.
//
// The update server sends full 16 KB TLS records, and wolfSSL allocates a ~16.4 KB buffer for each
// record and frees it once the record is read: ~390 times for one image. Between two records the
// Wi-Fi driver and lwIP take pieces of the freed space, and measured on the X3 the largest free
// block fell from 32,756 to 9,204 bytes within 5 s: the next record could not be allocated (wolfSSL
// MEMORY_E, -125) and the install failed at 219 KB. The slot takes the block once, at the first
// record that big, and hands the same block back for every later one.
namespace tls_slot {

constexpr size_t BYTES = 16384 + 512;    // a TLS 1.3 record (2^14 + 256) with wolfSSL's header and alignment
constexpr size_t MIN_BYTES = 12 * 1024;  // only record buffers ask this much

class Slot {
 public:
  using Malloc = void* (*)(size_t);
  using Free = void (*)(void*);
  Slot(Malloc allocate, Free release) : allocate_(allocate), release_(release) {}

  // The block for a request this size, or nullptr: the caller allocates normally.
  void* take(const size_t size) {
    if (size < MIN_BYTES) return nullptr;
    if (size > BYTES || inUse_) {
      ++fallbacks_;
      return nullptr;
    }
    if (!block_) block_ = allocate_(BYTES);
    if (!block_) {
      ++fallbacks_;
      return nullptr;
    }
    inUse_ = true;
    ++served_;
    return block_;
  }

  bool owns(const void* p) const { return p != nullptr && p == block_; }

  // True when p was the slot's block: it stays allocated for the next record.
  bool give(void* p) {
    if (!owns(p)) return false;
    inUse_ = false;
    return true;
  }

  // Ends the slot: frees the block when idle. A block still in use is left to its owner, who
  // frees it with the plain allocator (it is an ordinary allocation).
  void end() {
    if (block_ && !inUse_) release_(block_);
    block_ = nullptr;
    inUse_ = false;
  }

  uint32_t served() const { return served_; }
  uint32_t fallbacks() const { return fallbacks_; }

 private:
  Malloc allocate_;
  Free release_;
  void* block_ = nullptr;
  bool inUse_ = false;
  uint32_t served_ = 0;
  uint32_t fallbacks_ = 0;
};

// Routes wolfSSL's allocations through one slot while it lives (device only). Not nestable.
class Scope {
 public:
  Scope();
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

}  // namespace tls_slot
