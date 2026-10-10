#ifndef UGLY_MARK_SEED_H
#define UGLY_MARK_SEED_H

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace ugly {

inline std::atomic<uint32_t> markCounter{0};

inline void resetMarkSeed(uint32_t seed = 0) { markCounter.store(seed, std::memory_order_relaxed); }

inline uint32_t markSeed() {
#ifdef SIMULATOR
  // A measuring run pins every mark's seed (CROSSPOINT_SIM_MARK_SEED): the count of marks drawn depends on timing, so
  // the counter below would give two runs of one journey different hands.
  static const char* pinned = std::getenv("CROSSPOINT_SIM_MARK_SEED");
  if (pinned && pinned[0]) return static_cast<uint32_t>(std::atoll(pinned));
#endif
  uint32_t seed = markCounter.fetch_add(1, std::memory_order_relaxed) + 1;
  seed ^= seed >> 16;
  seed *= 0x7feb352du;
  seed ^= seed >> 15;
  seed *= 0x846ca68bu;
  return seed ^ (seed >> 16);
}

class MarkState {
 public:
  uint32_t get(uint64_t key) const {
    if (!valid_ || key != key_) {
      key_ = key;
      seed_ = markSeed();
      valid_ = true;
    }
    return seed_;
  }
  void reset() { valid_ = false; }

 private:
  mutable uint64_t key_ = 0;
  mutable uint32_t seed_ = 0;
  mutable bool valid_ = false;
};

}

#endif
