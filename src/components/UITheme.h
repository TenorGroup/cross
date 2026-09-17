#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  enum class StatusBarScope { Global, Reader };

  const ThemeMetrics& getMetrics() const;
  const BaseTheme& getTheme() const { return *currentTheme; }
  // Vung an toan = man hinh tru dai day that: dai nhan nut VA lan trang thai cua
  // dung pham vi dang ve (menu ngoai hay trong trinh doc), lay gia tri lon hon.
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false, StatusBarScope scope = StatusBarScope::Global);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  // Thanh trang thai co hai pham vi doc lap: ngoai trinh doc (menu) va trong
  // trinh doc. Cung mot ham tra chieu cao that cho tung pham vi.
  // Thanh trang thai co hai pham vi doc lap: ngoai trinh doc (menu) va trong
  // trinh doc. Cung mot ham tra chieu cao that cho tung pham vi.
  static int getStatusBarHeight(StatusBarScope scope = StatusBarScope::Global);
  static int getProgressBarHeight();

 private:
  const ThemeMetrics* currentMetrics;
  std::unique_ptr<BaseTheme> currentTheme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForTouch = false;
  mutable bool metricsForHiddenStatusBar = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
