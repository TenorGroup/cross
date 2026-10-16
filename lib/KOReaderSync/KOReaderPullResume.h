#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "../BlePageTurner/src/RadioPolicy.h"

namespace kosyncauto {

constexpr uint32_t PULL_ARMED_MAGIC = 0x4b415233;
constexpr uint32_t PULL_DONE_MAGIC = 0x4b444e33;
constexpr uint32_t PULL_NETWORK_MS = 12000;
// The pull keeps 4 s for GET and TLS after the saved-network join. A join may use at most 8 s
// inside the 12 s network phase, matching the measured scan path while leaving the request floor.
constexpr uint32_t PULL_JOIN_MS = PULL_NETWORK_MS - 4000;
// X3, 15/10: opening used 16,632 B after the Home pull (12,876 B on wake).
// Keep 3,848 B above the larger observation, including room in the largest block.
constexpr size_t kPullReaderReserveBytes = 20480;

// Plain bytes in RTC: the book and its return place outlive the restart before the page.
struct PullResume {
  uint32_t magic;
  char path[176];
  uint16_t fromSpine, fromPage, toSpine, toPage;
  uint32_t fromOffset, token, place;
  uint8_t hasFromOffset, hasChange, hasMark, useStoredPath;
};
static_assert(sizeof(PullResume) == 204);

inline bool validPullResume(const PullResume& kept) {
  return (kept.magic == PULL_ARMED_MAGIC || kept.magic == PULL_DONE_MAGIC) && kept.path[0] == '/' &&
         std::memchr(kept.path, 0, sizeof(kept.path)) && kept.hasFromOffset <= 1 && kept.hasChange <= 1 &&
         kept.hasMark <= 1 && kept.useStoredPath <= 1;
}

inline bool keepPullPath(PullResume& kept, const std::string& path, const uint32_t magic,
                         const bool useStoredPath = false) {
  kept = {};
  if (path.empty() || path.front() != '/' || (!useStoredPath && path.size() >= sizeof(kept.path))) return false;
  if (useStoredPath) {
    kept.path[0] = '/';  // The wake path is already saved in the application state.
    kept.useStoredPath = 1;
  } else {
    std::memcpy(kept.path, path.c_str(), path.size() + 1);
  }
  kept.magic = magic;
  return true;
}

inline uint32_t takePullMagic(PullResume& kept, const bool quietRestart) {
  const uint32_t magic = quietRestart && validPullResume(kept) ? kept.magic : 0;
  kept.magic = 0;
  return magic;
}

constexpr bool restartAfterPull(const bool enabled, const bool usedNetwork, const bool succeeded, const bool spent,
                                const size_t freeBytes, const size_t largest) {
  if (!enabled || !usedNetwork || !succeeded || spent) return false;
  auto need = bleturner::kX3Profile;
  need.startFreeBytes += kPullReaderReserveBytes;
  need.minimumLargest += kPullReaderReserveBytes;
  bleturner::RadioInputs in{};
  in.phase = bleturner::Phase::BeforeStart;
  in.where = bleturner::Where::Reader;
  in.heap = {freeBytes, largest};
  return bleturner::radioVerdict(in, need) == bleturner::Why::HeapInPieces;
}

constexpr uint32_t pullRequestMs(const uint32_t elapsed) {
  return elapsed >= PULL_NETWORK_MS ? 0 : (PULL_NETWORK_MS - elapsed > 6000 ? 6000 : PULL_NETWORK_MS - elapsed);
}
constexpr bool pullNetworkExpired(const uint32_t elapsed) { return elapsed >= PULL_NETWORK_MS; }

}  // namespace kosyncauto
