#pragma once
#include <cstdint>

namespace shell {

enum class Kind : uint8_t { Cross = 0, Ugly = 1 };

inline Kind kindOf(const uint8_t saved) {
  return saved == static_cast<uint8_t>(Kind::Ugly) ? Kind::Ugly : Kind::Cross;
}

}  // namespace shell
