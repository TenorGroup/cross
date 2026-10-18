#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  // One line per '\n', the block centred on the screen.
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto lines = 1 + std::count(text.begin(), text.end(), '\n');
  auto y = (renderer.getScreenHeight() - height * static_cast<int>(lines)) / 2;

  renderer.clearScreen();
  for (size_t start = 0;; y += height) {
    const size_t end = text.find('\n', start);
    renderer.drawCenteredText(UI_10_FONT_ID, y, text.substr(start, end - start).c_str(), true, style);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  renderer.displayBuffer(refreshMode);
}
