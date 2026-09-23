#pragma once
#include "TenorRadius.h"
#include "lyra/LyraTheme.h"

namespace TenorMetrics {
inline constexpr ThemeMetrics values = [] {
  auto m = LyraMetrics::values;
  m.headerUnderlineSize = 1;
  m.listSelectionStyle = 0;
  // Every radius from the corner formula (TenorRadius.h). A FreeInkUI list row is 52 px tall at
  // the smallest UI size (listRowHeight, measured on the simulator's Settings list): a leaf of 7.
  m.listRowRadius = tenorradius::leaf(m.listRowHeight);
  m.listRowGap = 4;
  m.listRowHeight = 52;
  m.listWithSubtitleRowHeight = 72;
  m.keyboardKeySpacing = 3;
  // Tiles, step buttons and the capsule slider are leaves the height of a row.
  m.controlRadius = m.listRowRadius;
  m.capsuleRadius = m.listRowRadius;
  // Popups and sheets hold rows set in by the option popup's padding (20 px): concentric with
  // them, 7 + 20 = 27.
  m.popupCornerRadius = tenorradius::container(m.listRowRadius, m.optionPopupInnerPadding);
  m.sheetRadius = m.popupCornerRadius;
  m.roundedMarks = true;
  return m;
}();
}  // namespace TenorMetrics
class TenorTheme final : public LyraTheme {
 public:
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
};
