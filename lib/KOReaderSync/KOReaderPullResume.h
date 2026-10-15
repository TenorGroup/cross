#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace kosyncauto {

constexpr uint32_t PULL_ARMED_MAGIC = 0x4b415232;
constexpr uint32_t PULL_DONE_MAGIC = 0x4b444e32;
constexpr uint32_t PULL_NETWORK_MS = 12000;

// Plain bytes in RTC: the book and its return place outlive the restart before the page.
struct PullResume {
  uint32_t magic;
  char path[176];
  uint16_t fromSpine, fromPage, toSpine, toPage;
  uint32_t fromOffset, token, place;
  uint8_t hasFromOffset, hasChange, hasMark;
};

inline bool validPullResume(const PullResume& kept) {
  return (kept.magic == PULL_ARMED_MAGIC || kept.magic == PULL_DONE_MAGIC) && kept.path[0] == '/' &&
         std::memchr(kept.path, 0, sizeof(kept.path)) && kept.hasFromOffset <= 1 && kept.hasChange <= 1 &&
         kept.hasMark <= 1;
}

inline bool keepPullPath(PullResume& kept, const std::string& path, const uint32_t magic) {
  kept = {};
  if (path.empty() || path.front() != '/' || path.size() >= sizeof(kept.path)) return false;
  std::memcpy(kept.path, path.c_str(), path.size() + 1);
  kept.magic = magic;
  return true;
}

inline uint32_t takePullMagic(PullResume& kept, const bool quietRestart) {
  const uint32_t magic = quietRestart && validPullResume(kept) ? kept.magic : 0;
  kept.magic = 0;
  return magic;
}

constexpr bool restartAfterPull(const bool enabled, const bool usedNetwork, const bool succeeded, const bool spent,
                                const size_t freeBytes, const size_t largest, const size_t needFree,
                                const size_t needLargest) {
  return enabled && usedNetwork && succeeded && !spent && (freeBytes < needFree || largest < needLargest);
}

constexpr uint32_t pullRequestMs(const uint32_t elapsed) {
  return elapsed >= PULL_NETWORK_MS ? 0 : (PULL_NETWORK_MS - elapsed > 6000 ? 6000 : PULL_NETWORK_MS - elapsed);
}
constexpr bool pullNetworkExpired(const uint32_t elapsed) { return elapsed >= PULL_NETWORK_MS; }

}  // namespace kosyncauto
