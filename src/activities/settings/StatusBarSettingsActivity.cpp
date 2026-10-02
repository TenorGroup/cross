#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "components/UITheme.h"
#include "components/SettledListRender.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Menu items in their natural order. The clock position entry is appended only
// when the RTC probe found hardware; time, zone, and format live in Settings >
// System > Clock.
enum MenuItem {
  ITEM_CHAPTER_PAGE_COUNT = 0,
  ITEM_BOOK_PROGRESS_PERCENTAGE,
  ITEM_PROGRESS_BAR,
  ITEM_PROGRESS_BAR_THICKNESS,
  ITEM_TITLE,
  ITEM_BATTERY,
  ITEM_XTC_STATUS_BAR,
  ITEM_CLOCK,  // RTC boards only
};

constexpr int PROGRESS_BAR_ITEMS = 3;
constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
constexpr int TITLE_ITEMS = 3;
constexpr int XTC_STATUS_BAR_ITEMS = 3;
constexpr int STATUS_BAR_CLOCK_ITEMS = CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT;

// The Tenor bar draws the chapter name, the two counts, battery and clock, so its
// screen lists only the items that bar reads, as the classic item each one edits.
constexpr MenuItem TENOR_ROWS[] = {ITEM_TITLE, ITEM_CHAPTER_PAGE_COUNT, ITEM_BOOK_PROGRESS_PERCENTAGE, ITEM_CLOCK};
constexpr StrId TENOR_NAMES[] = {StrId::STR_CHAPTER_NAME, StrId::STR_CHAPTER_PAGE_COUNT,
                                 StrId::STR_BOOK_PROGRESS_PERCENTAGE, StrId::STR_STATUS_CORNERS};
constexpr int TENOR_ROW_COUNT = sizeof(TENOR_ROWS) / sizeof(TENOR_ROWS[0]);
static_assert(TENOR_ROW_COUNT == StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS,
              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with TENOR_ROWS");

int itemAt(const int row) { return TENOR_ROWS[row]; }
}  // namespace

StatusBarSettingsActivity::StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

void StatusBarSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  visibleItemCount = TENOR_ROW_COUNT;
  // A mode picked in the reader menu or on the web shows what its name says; the
  // switches take that as their starting point before any of them is flipped.
  SETTINGS.adoptReaderStatusItems();

  // Clamp statusBarProgressBar and statusBarTitle in case of corrupt/migrated data
  if (SETTINGS.statusBarProgressBar >= PROGRESS_BAR_ITEMS) {
    SETTINGS.statusBarProgressBar = CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  }

  if (SETTINGS.statusBarProgressBarThickness >= PROGRESS_BAR_THICKNESS_ITEMS) {
    SETTINGS.statusBarProgressBarThickness = CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL;
  }

  if (SETTINGS.statusBarTitle >= TITLE_ITEMS) {
    SETTINGS.statusBarTitle = CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE;
  }

  if (SETTINGS.xtcStatusBarMode >= XTC_STATUS_BAR_ITEMS) {
    SETTINGS.xtcStatusBarMode = CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_HIDE;
  }

  if (SETTINGS.statusBarClock >= STATUS_BAR_CLOCK_ITEMS) {
    SETTINGS.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_MODE::STATUS_BAR_CLOCK_HIDE;
  }

  // Labels never change (unlike the values, which track live SETTINGS
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < visibleItemCount; i++) {
    rowItems_[i].label = I18N.get(TENOR_NAMES[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

bool StatusBarSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection() {
  switch (itemAt(nav.selected)) {
    case ITEM_CHAPTER_PAGE_COUNT:
      SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2;
      break;
    case ITEM_BOOK_PROGRESS_PERCENTAGE:
      SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
      break;
    case ITEM_TITLE:
      // Tenor names the chapter only: the switch is chapter name shown or hidden.
      SETTINGS.statusBarTitle = SETTINGS.statusBarTitle == CrossPointSettings::HIDE_TITLE
                                    ? CrossPointSettings::CHAPTER_TITLE
                                    : CrossPointSettings::HIDE_TITLE;
      break;
    case ITEM_CLOCK:
      // Tenor always draws the clock the mode asks for; this row only swaps corners.
      SETTINGS.statusBarClock = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT
                                    ? CrossPointSettings::STATUS_BAR_CLOCK_RIGHT
                                    : CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

std::string StatusBarSettingsActivity::rowValueText(const int index) {
  switch (itemAt(index)) {
    case ITEM_CHAPTER_PAGE_COUNT:
      return SETTINGS.statusBarChapterPageCount ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_BOOK_PROGRESS_PERCENTAGE:
      return SETTINGS.statusBarBookProgressPercentage ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_TITLE:
      return SETTINGS.statusBarTitle != CrossPointSettings::HIDE_TITLE ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_CLOCK:
      return SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT ? tr(STR_CLOCK_LEFT_BATTERY_RIGHT)
                                                                                  : tr(STR_BATTERY_LEFT_CLOCK_RIGHT);
    default:
      return tr(STR_HIDE);
  }
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_'s labels/actionValue were set once in onEnter(); only the live
  // value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  for (int i = 0; i < visibleItemCount; i++) {
    rowValues_[i] = rowValueText(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(visibleItemCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;  // also the explicitly-set marker, see SettingsActivity
  syncListViewport(screen, props);
  screen.list(props);
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  const auto pageWidth = renderer.getScreenWidth();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the list renders through the app.
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawNavigationHeader(tr(STR_CUSTOMISE_STATUS_BAR));
    renderUi();
  });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

std::string StatusBarSettingsActivity::favoriteKey(int row) const {
  // Tenor pins only the corner row: the favorites catalog labels every other
  // status pin as empty there, so a pinned switch would show no name.
  if (itemAt(row) != ITEM_CLOCK) return {};
  return menufavorites::keyFor("status", 0, itemAt(row));
}
