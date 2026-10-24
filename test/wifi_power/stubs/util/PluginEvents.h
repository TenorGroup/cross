#pragma once
#include <cstddef>

class GfxRenderer;

// The outbox drain the chooser runs on every station join; nothing is queued in this harness.
namespace pluginevents {
inline void drain(GfxRenderer*, size_t = 4) {}
}  // namespace pluginevents
