#pragma once

#include <cstdint>

#include "CrossPointSettings.h"

// Gan nut cho page turner BLE. Tach khoi man Cai dat de bai kiem host bom duoc
// phim gia qua dung ham nay thay vi chep lai luat ra test.
namespace blebinding {

// Mot luot cho phim toi da 15 giay, dung nhu man Cai dat hen voi nguoi dung.
constexpr uint32_t kWaitMs = 15000;

enum class Direction : uint8_t { Next, Prev };

// Ma usage HID THO. 0 = chua gan gi.
constexpr uint8_t kUnassigned = CrossPointSettings::BLE_USAGE_NONE;

// Phim dau tien co usage khac 0 duoc gan. Khung nha nut (usage 0) khong tinh, va
// phim di kem modifier cung khong tinh - giong luat cua ban doc.
constexpr bool usableUsage(const uint8_t usage, const uint8_t mods) { return usage != kUnassigned && mods == 0; }

// Ma dang gan cua mot chieu: 0 = chua gan, con lai la usage se lat trang.
inline uint8_t assigned(const Direction direction) {
  return direction == Direction::Next ? SETTINGS.bleNextKeyUsage : SETTINGS.blePrevKeyUsage;
}

// Ghi ma vua nhan vao dung chieu. Tra ve false khi khong hoc duoc gi.
inline bool assign(const Direction direction, const uint8_t usage, const uint8_t mods) {
  if (!usableUsage(usage, mods)) return false;
  if (direction == Direction::Next) {
    SETTINGS.bleNextKeyUsage = usage;
  } else {
    SETTINGS.blePrevKeyUsage = usage;
  }
  return true;
}

// Bo gan cua mot chieu (hang "Xoa gan").
inline void clear(const Direction direction) {
  if (direction == Direction::Next) {
    SETTINGS.bleNextKeyUsage = kUnassigned;
  } else {
    SETTINGS.blePrevKeyUsage = kUnassigned;
  }
}

}  // namespace blebinding
