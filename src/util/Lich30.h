#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "ReadingHabits.h"
#include "SoLieuDoc.h"

// Luoi 30 o cua man "30 ngay qua". Thuan, khong dung toi phan cung, nen bai kiem chay tren may de ban
// (test/lich_30). Hai quyet dinh nam o day va chi o day: o nao lay dong nao cua kho (tinh) va moi o
// dam may bac (muc). Man hinh chi to mau theo ket qua.
namespace lich30 {

constexpr int SO_O = 30;
// Bac theo moc phut co dinh, nen mau mot o chi tuy ngay cua no: duoi 1 gio la bac 1, tu 1 gio toi duoi 3 gio
// la bac 2, tu 3 gio la bac 3. Hai moc chia 13 ngay doc that cua founder (26/09/2026) thanh 2, 6, 5 ngay.
constexpr uint32_t MOC_BAC_2_MS = 60u * 60000u;
constexpr uint32_t MOC_BAC_3_MS = 180u * 60000u;

// Bac 0 = khong doc.
inline uint8_t muc(const uint32_t ms) { return ms == 0 ? 0 : ms < MOC_BAC_2_MS ? 1 : ms < MOC_BAC_3_MS ? 2 : 3; }

// Dong cua kho ung voi o i (0 = 29 ngay truoc, 29 = hom nay), hoac nullptr. ReadingHistoryActivity
// tinh lai moc ngay cua cot va cua tuan (firstKey, weekKey): doi cua so o day thi doi ca o do.
inline const solieu::NgayDoc* dongCua(const std::vector<solieu::NgayDoc>& ngay, const uint32_t homNay, const int i) {
  const uint32_t ma = habits::dateKey(homNay - (SO_O - 1) + i);
  for (const auto& n : ngay)
    if (n.ma == ma) return &n;
  return nullptr;
}

// ra[0] la 29 ngay truoc, ra[29] la hom nay. `homNay` la thu tu ngay (habits::ordinal) va bang 0 khi may
// chua biet ngay: khi do khong co o nao de ve, tra false.
inline bool tinh(const std::vector<solieu::NgayDoc>& ngay, const uint32_t homNay, uint8_t (&ra)[SO_O]) {
  std::fill(ra, ra + SO_O, uint8_t{0});
  if (homNay < SO_O) return false;
  for (int i = 0; i < SO_O; ++i) {
    const auto* n = dongCua(ngay, homNay, i);
    if (!n) continue;
    // Ngay chi co luot lat (khong co thoi gian) van la ngay da doc.
    const uint32_t ms = n->phut * 60000u + n->leMs;
    ra[i] = muc(ms == 0 && n->trang ? 1 : ms);
  }
  return true;
}

// Bieu do gio doc theo tuan: SO_TUAN khung 7 ngay, ra[0] cu nhat, ra[SO_TUAN - 1] la 7 ngay ket thuc hom nay.
// Moi khung la phut (cong ms roi cat xuong). Khung k lay cac o tinh() tu (SO_O - 7 * SO_TUAN + 7 * k) len 7 o,
// nen hai ham chung mot cua so; 2 o xa nhat cua luoi nam ngoai bieu do.
constexpr int SO_TUAN = 4;
inline bool tuan(const std::vector<solieu::NgayDoc>& ngay, const uint32_t homNay, uint32_t (&ra)[SO_TUAN]) {
  std::fill(ra, ra + SO_TUAN, uint32_t{0});
  if (homNay < SO_O) return false;
  constexpr int dau = SO_O - 7 * SO_TUAN;
  for (int k = 0; k < SO_TUAN; ++k) {
    uint64_t ms = 0;
    for (int i = dau + 7 * k; i < dau + 7 * k + 7; ++i)
      if (const auto* n = dongCua(ngay, homNay, i)) ms += n->phut * 60000u + n->leMs;
    ra[k] = static_cast<uint32_t>(ms / 60000u);
  }
  return true;
}

}  // namespace lich30
