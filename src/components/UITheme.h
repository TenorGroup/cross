#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/TenorTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  enum class StatusBarScope { Global, Reader };

  const ThemeMetrics& getMetrics() const;
  // Falls back to a built-in Classic theme while no theme could be allocated.
  const BaseTheme& getTheme() const { return currentTheme ? *currentTheme : fallbackTheme; }
  // The drawable content area: full screen minus the board's viewable insets
  // (bezel / rounded-corner clearance from BoardConfig::ACTIVE.viewableInsets,
  // oriented to the current rotation). THE single place activities should get
  // their content bounds from so every screen clears the bezel identically.
  // Zero-inset on rectangular panels, so no visible change there.
  static Rect getContentArea(const GfxRenderer& renderer);
  // Vung an toan = man hinh tru dai day that: dai nhan nut VA lan trang thai cua
  // dung pham vi dang ve (menu ngoai hay trong trinh doc), lay gia tri lon hon.
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false, StatusBarScope scope = StatusBarScope::Global);
  // Header rect for GUI.drawHeader() on a screen laid out in getScreenSafeArea():
  // clear of the button hints but NOT inset for the bezel, since the header band
  // applies the board's viewable insets itself.
  Rect getHeaderRect(const GfxRenderer& renderer, bool hasFrontButtonHints = false, bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  void reload();
  void setTheme();
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverWidth, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  // Thanh trang thai co hai pham vi doc lap: ngoai trinh doc (menu) va trong
  // trinh doc. Cung mot ham tra chieu cao that cho tung pham vi.
  // Thanh trang thai co hai pham vi doc lap: ngoai trinh doc (menu) va trong
  // trinh doc. Cung mot ham tra chieu cao that cho tung pham vi.
  static int getStatusBarHeight(StatusBarScope scope = StatusBarScope::Global);
  static int getProgressBarHeight();

 private:
  BaseTheme fallbackTheme;
  const ThemeMetrics* currentMetrics = &TenorMetrics::values;
  std::unique_ptr<BaseTheme> currentTheme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable uint8_t metricsForTextSize = 0;
  mutable bool metricsForTouch = false;
  mutable bool metricsForHiddenStatusBar = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
