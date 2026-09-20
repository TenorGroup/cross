#pragma once
#include <GfxRenderer.h>
struct Rect { int x, y, width, height; };
struct TestSettings { int keyboardAxisSwapped = 0, keyboardAligned = 0; uint8_t uiTextSize = 0; };
inline TestSettings SETTINGS;
struct ThemeMetrics {
  int topPadding = 4, headerHeight = 24, verticalSpacing = 4, keyboardVerticalOffset = 0;
  int sideButtonHintsWidth = 0, keyboardTextFieldWidthPercent = 90;
  bool keyboardCenteredText = false;
  int keyboardKeySpacing = 4, keyboardKeyHeight = 32, keyboardWidthPercent = 90, buttonHintsHeight = 24;
};
class UITheme {
 public:
  static UITheme& getInstance() { static UITheme instance; return instance; }
  const ThemeMetrics& getMetrics() const {
    static ThemeMetrics metrics;
    metrics = ThemeMetrics{};
    if (SETTINGS.uiTextSize) {
      metrics.topPadding = 5; metrics.headerHeight = SETTINGS.uiTextSize == 1 ? 94 : 106;
      metrics.verticalSpacing = 16; metrics.keyboardVerticalOffset = -7;
      metrics.keyboardKeySpacing = 3; metrics.keyboardKeyHeight = SETTINGS.uiTextSize == 1 ? 53 : 58;
      metrics.keyboardWidthPercent = 94; metrics.buttonHintsHeight = SETTINGS.uiTextSize == 1 ? 80 : 90;
    }
    return metrics;
  }
  template <typename... T> void drawHeader(T&&...) const {}
  template <typename... T> void drawTextField(T&&...) const {}
  template <typename... T> void drawButtonHints(T&&...) const {}
  template <typename... T> void drawSideButtonHints(T&&...) const {}
};
#define GUI UITheme::getInstance()
