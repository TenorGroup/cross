#pragma once
#include "TenorRadius.h"
#include "lyra/LyraTheme.h"

namespace TenorMetrics {
inline constexpr ThemeMetrics values = [] {
  auto m = LyraMetrics::values;
  m.headerUnderlineSize = 1;
  m.listSelectionStyle = 0;
  m.listRowGap = 4;
  m.listRowHeight = 52;
  // Every radius from the corner formula (TenorRadius.h), taken after the row height above is
  // set: read before it, the formula would see the 40 px row of the theme this starts from. A
  // FreeInkUI list row is 52 px tall at the smallest UI size (measured on the simulator's
  // Settings list): a leaf of 8.
  m.listRowRadius = tenorradius::leaf(m.listRowHeight);
  m.listWithSubtitleRowHeight = 72;
  m.keyboardKeySpacing = 3;
  // Tiles, step buttons and the capsule slider are leaves the height of a row.
  m.controlRadius = m.listRowRadius;
  m.capsuleRadius = m.listRowRadius;
  // Popups and sheets hold rows set in by the option popup's padding (20 px): concentric with
  // them, 8 + 20 = 28.
  m.popupCornerRadius = tenorradius::container(m.listRowRadius, m.optionPopupInnerPadding);
  m.sheetRadius = m.popupCornerRadius;
  m.roundedMarks = true;
  return m;
}();
// Locked here so a reordering above cannot quietly shrink every corner again.
static_assert(values.listRowRadius == tenorradius::leaf(values.listRowHeight) && values.listRowRadius == 8);
static_assert(values.popupCornerRadius ==
                  tenorradius::container(values.listRowRadius, values.optionPopupInnerPadding) &&
              values.popupCornerRadius == 28);
}  // namespace TenorMetrics
class TenorTheme final : public LyraTheme {
 public:
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
};
