#include "ClearCacheActivity.h"
#include "X3SettingsLayout.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglySwitch.h"
#include "shells/ugly/UglyWords.h"
#include "util/BookCacheUtils.h"

namespace {
// tenor/ugly on the button readers writes the lines of this screen by hand, centred; S22 when S30 is too wide.
void sayCentred(const GfxRenderer& r, const int baseline, const char* text) {
  const ugly::Size size = ugly::width(r, ugly::Size::S30, text) <= r.getScreenWidth() - 24 ? ugly::Size::S30 : ugly::Size::S22;
  ugly::text(r, size, (r.getScreenWidth() - ugly::width(r, size, text)) / 2, baseline, text);
}
}  // namespace

void ClearCacheActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  // Touch: the warning goes inside the question, which offers its one action; "<" on the bar cancels.
  const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAR_BUTTON)};
  constexpr int skip = tenorchrome::kTouchShell ? 1 : 0;
  const auto onSelect = [this](int idx) {
    if (idx + skip == 1) {
      beginClear();
    } else {
      goBack();
    }
  };
  if (tenorchrome::kTouchShell) {
    const std::string warning = std::string(tr(STR_CLEAR_CACHE_WARNING_1)) + " " + tr(STR_CLEAR_CACHE_WARNING_2) + " " +
                                tr(STR_CLEAR_CACHE_WARNING_3) + " " + tr(STR_CLEAR_CACHE_WARNING_4);
    confirmPopup.show(tr(STR_CLEAR_READING_CACHE), warning.c_str(), options + skip, 2 - skip, -1, onSelect);
  } else {
    confirmPopup.show(tr(STR_CLEAR_READING_CACHE), options, 2, 0, onSelect);
  }
  requestUpdate();
}

void ClearCacheActivity::onExit() { Activity::onExit(); }

void ClearCacheActivity::render(RenderLock&&) {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    renderX3();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  drawNavigationHeader(tr(STR_CLEAR_READING_CACHE));

  const bool handwritten = shell::uglyParts();
  if (state == WARNING && handwritten) {
    // The question box of the shell: the warning is its note, the circle follows the popup that takes the keys.
    const std::string note = std::string(tr(STR_CLEAR_CACHE_WARNING_1)) + " " + tr(STR_CLEAR_CACHE_WARNING_2) + " " +
                             tr(STR_CLEAR_CACHE_WARNING_3) + " " + tr(STR_CLEAR_CACHE_WARNING_4);
    const char* answers[2] = {tr(STR_CANCEL), tr(STR_CLEAR_BUTTON)};
    ugly::Box drawn[2];
    ugly::askBox(renderer, mappedInput, tr(STR_CLEAR_READING_CACHE), note.c_str(), answers, confirmPopup.selected(), drawn,
                 confirmPopup.selectionSeed());
    renderer.displayBuffer();
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=ask sel=%d", confirmPopup.selected());
#endif
    return;
  }
  if (state == WARNING) {
    if (tenorchrome::kTouchShell && confirmPopup.processRender(renderer, mappedInput)) return;
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, tr(STR_CLEAR_CACHE_WARNING_1), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, tr(STR_CLEAR_CACHE_WARNING_2), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CLEAR_CACHE_WARNING_3), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_CLEAR_CACHE_WARNING_4), true);

    if (confirmPopup.processRender(renderer, mappedInput)) return;

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAR_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    if (handwritten) {
      sayCentred(renderer, pageHeight / 2 + 10, tr(STR_CLEARING_CACHE));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CLEARING_CACHE));
    }
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    std::string resultText = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    if (handwritten) {
      sayCentred(renderer, pageHeight / 2 - 10, ugly::words::cacheDone());
      sayCentred(renderer, pageHeight / 2 + 34, resultText.c_str());
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_CACHE_CLEARED), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    if (handwritten) {
      sayCentred(renderer, pageHeight / 2 - 10, ugly::words::cacheFail());
      sayCentred(renderer, pageHeight / 2 + 34, tr(STR_CHECK_SERIAL_OUTPUT));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_CLEAR_CACHE_FAILED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

std::string ClearCacheActivity::x3InfoText() const {
  if (state == WARNING)
    return std::string(tr(STR_CLEAR_CACHE_WARNING_1)) + "\n" + tr(STR_CLEAR_CACHE_WARNING_2) + "\n" +
           tr(STR_CLEAR_CACHE_WARNING_3) + "\n" + tr(STR_CLEAR_CACHE_WARNING_4);
  if (state == CLEARING) return tr(STR_CLEARING_CACHE);
  if (state == FAILED) return std::string(tr(STR_CLEAR_CACHE_FAILED)) + "\n" + tr(STR_CHECK_SERIAL_OUTPUT);
  std::string result = std::string(tr(STR_CACHE_CLEARED)) + "\n" + std::to_string(clearedCount) + " " + tr(STR_ITEMS_REMOVED);
  if (failedCount > 0) result += ", " + std::to_string(failedCount) + " " + tr(STR_FAILED_LOWER);
  return result;
}

void ClearCacheActivity::renderX3() {
  renderer.clearScreen();
  const auto layout = tenorchrome::x3ScreenLayout(renderer);
  auto info = layout.bodyRect;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  info.height -= state == WARNING ? 3 * lineHeight + 8 : lineHeight + 8;
  const std::string text = x3InfoText();
  infoPages_ = x3settings::infoPageCount(renderer, text.c_str(), info);
  infoPage_ = std::clamp(infoPage_, 0, infoPages_ - 1);
  x3settings::drawInfoPage(renderer, text.c_str(), info, infoPage_);
  auto row = layout.bodyRect;
  row.y = info.bottom() + 8;
  row.height = lineHeight;
  if (state == WARNING) {
    const std::string cancel = std::string(confirmPopup.selected() == 0 ? "> " : "  ") + tr(STR_CANCEL);
    const std::string clear = std::string(confirmPopup.selected() == 1 ? "> " : "  ") + tr(STR_CLEAR_BUTTON);
    tenorchrome::drawInfoInRect(renderer, cancel.c_str(), row, 1);
    row.y += lineHeight;
    tenorchrome::drawInfoInRect(renderer, clear.c_str(), row, 1);
    row.y += lineHeight;
  }
  if (infoPages_ > 1) {
    const std::string position = std::to_string(infoPage_ + 1) + "/" + std::to_string(infoPages_);
    tenorchrome::drawInfoInRect(renderer, position.c_str(), row, 1);
  }
  x3settings::drawPath(renderer, tr(STR_CLEAR_READING_CACHE));
  const auto labels = mappedInput.mapLabels(state == WARNING ? tr(STR_CANCEL) : state == CLEARING ? "" : tr(STR_BACK),
                                             state == WARNING ? tr(STR_SELECT) : "",
                                             state == WARNING || infoPages_ > 1 ? tr(STR_DIR_UP) : "",
                                             state == WARNING || infoPages_ > 1 ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ClearCacheActivity::beginClear() {
  LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
  {
    RenderLock lock(*this);
    state = CLEARING;
  }
  requestUpdateAndWait();
  clearCache();
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    state = FAILED;
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    // Only delete directories matching known book cache names.
    if (file.isDirectory() && isBookCacheDirectoryName(itemName.c_str())) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  state = SUCCESS;
  requestUpdate();
}

void ClearCacheActivity::loop() {
  if (tenorchrome::x3ScreenLayoutEnabled() && infoPages_ > 1) {
    const bool previous = mappedInput.wasPressed(state == WARNING ? MappedInputManager::Button::Left
                                                                  : MappedInputManager::Button::Up);
    const bool next = mappedInput.wasPressed(state == WARNING ? MappedInputManager::Button::Right
                                                              : MappedInputManager::Button::Down);
    if (previous || next) {
      RenderLock lock(*this);
      infoPage_ = std::clamp(infoPage_ + (next ? 1 : -1), 0, infoPages_ - 1);
      requestUpdate();
      return;
    }
  }
  if (state == WARNING) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // Touch: the question closed without its action ("<" on the bar, a tap outside): back.
    if (tenorchrome::kTouchShell) {
      goBack();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      beginClear();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG("CLEAR_CACHE", "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      goBack();
    }
    return;
  }
}
