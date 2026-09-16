#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

namespace fontdoc {

struct Ho {
  std::string ten;
  bool builtin;
  uint8_t chiSo;  // gia tri ghi vao SETTINGS.fontFamily, hoac BUILTIN_FONT_COUNT + vi tri tren the
};

// Hai ho nap san truoc, roi cac ho tren the theo thu tu registry.
std::vector<Ho> danhSachHo(const SdCardFontRegistry* registry);

// Chi so trong danhSachHo() cua ho dang dung. Ho tren the da mat thi lui ve ho nap san.
int hoDangDung(const SdCardFontRegistry* registry);

// Ap ho font o chi so `index` cua danhSachHo(). GOI DUOI RenderLock: ensureLoaded() thay font
// ma tac ve co the dang doc. KHONG luu settings, nguoi goi luu sau khi nha khoa. Tra ve false
// neu chi so ngoai mien.
bool apHo(GfxRenderer& renderer, const SdCardFontRegistry* registry, int index);

// Chi so cua co dang dung trong `sizes` (da nan ve co gan nhat ma ho font co).
int coDangDung(const std::vector<uint8_t>& sizes);

// Ap co chu. GOI DUOI RenderLock. KHONG luu settings.
void apCo(GfxRenderer& renderer, uint8_t pt);

}  // namespace fontdoc
