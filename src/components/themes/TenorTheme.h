#pragma once
#include "lyra/LyraTheme.h"

namespace TenorMetrics {
inline constexpr ThemeMetrics values = [] {
  auto m = LyraMetrics::values;
  m.headerUnderlineSize = 1;
  m.listSelectionStyle = 0;
  m.listRowRadius = 3;
  m.listRowGap = 4;
  m.listRowHeight = 52;
  m.listWithSubtitleRowHeight = 72;
  m.keyboardKeySpacing = 3;
  m.controlRadius = 3;
  m.sheetRadius = 3;
  m.capsuleRadius = 3;
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
