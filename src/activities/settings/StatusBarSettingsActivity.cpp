#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/UglyQuip.h"
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
  RenderLock lock(*this);
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
  if (shell::isUgly()) {
    ugly::ensureFonts(renderer);
    bindForm();
  }
}

void StatusBarSettingsActivity::queueForm(FormEvent event) {
  if (event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold)
    event.surface = formVisibleSurface_.load();
  if (formCount_ < formQueue_.size()) formQueue_[(formHead_ + formCount_++) % formQueue_.size()] = event;
  else LOG_ERR("UGLY", "Settings form queue full");
}

void StatusBarSettingsActivity::pollTilt() {
  if (!shell::isUgly()) UiListActivity::pollTilt();
}

bool StatusBarSettingsActivity::handleCustomInput() {
  if (!shell::isUgly()) return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
  using Button = MappedInputManager::Button;
  using Key = ugly::QuestionSheet::Key;
  const auto key = [this](Key value) { queueForm({FormEvent::Type::Key, value}); };
  if (mappedInput.wasLongPressed(Button::Left, 700)) key(Key::PreviousSheet);
  else if (mappedInput.wasReleased(Button::Left)) key(Key::PreviousQuestion);
  if (mappedInput.wasLongPressed(Button::Right, 700)) key(Key::NextSheet);
  else if (mappedInput.wasReleased(Button::Right)) key(Key::NextQuestion);
  if (mappedInput.wasReleased(Button::Up)) key(Key::PreviousOption);
  if (mappedInput.wasReleased(Button::Down)) key(Key::NextOption);
  if (mappedInput.wasLongPressed(Button::Confirm, 700)) queueForm({FormEvent::Type::Pin});
  else if (mappedInput.wasReleased(Button::Confirm)) key(Key::Confirm);
  const bool back = mappedInput.wasReleased(Button::Back);
  const bool home = mappedInput.wasHomeGesture();
  if (back) key(Key::Back);
  if (home) key(Key::Home);
  int x = 0, y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) queueForm({FormEvent::Type::Hold, Key::Confirm, static_cast<int16_t>(x), static_cast<int16_t>(y)});
  else if (mappedInput.wasScreenTapped(x, y)) queueForm({FormEvent::Type::Tap, Key::Confirm, static_cast<int16_t>(x), static_cast<int16_t>(y)});
  const auto swipe = mappedInput.wasSwipe();
  if (!back && !home) {
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) key(Key::NextSheet);
    else if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) key(Key::PreviousSheet);
  }
  if (!formCount_ || !formPaintReady_.load()) return true;
  ugly::QuestionSheet::Intent intent;
  int pinRow = -1;
  bool homeEvent = false;
  {
    RenderLock lock(RenderLock::TryTake{});
    if (!lock.acquired() || !formPaintReady_.load()) return true;
    const auto event = formQueue_[formHead_];
    formHead_ = static_cast<uint8_t>((formHead_ + 1) % formQueue_.size());
    --formCount_;
    // A finger event belongs to the surface that was visible when sampled.
    // A tap queued before a paper opened cannot choose a row on that paper.
    if ((event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold) && event.surface != formSurface_)
      return true;
    homeEvent = event.type == FormEvent::Type::Key && event.key == Key::Home;
    const bool paperWasOpen = form_.paperOpen();
    const int previousSheet = form_.sheet();
    const int previousPaperFirst = form_.paperFirst();
    if (event.type == FormEvent::Type::Key) intent = form_.input(event.key);
    else if (event.type == FormEvent::Type::Tap) intent = form_.tap(event.x, event.y);
    else if (!form_.paperOpen()) pinRow = event.type == FormEvent::Type::Hold ? form_.questionAt(event.x, event.y) : form_.question();
    if (paperWasOpen != form_.paperOpen() || previousSheet != form_.sheet() ||
        (form_.paperOpen() && previousPaperFirst != form_.paperFirst())) ++formSurface_;
    activeNav().selected = form_.question();
    if (intent.repaint || pinRow >= 0) formPaintReady_.store(false);
  }
  if (pinRow >= 0 && !favoriteKey(pinRow).empty()) {
    const bool wasPinned = rowIsPinned(pinRow);
    formPinFailed_.store(!toggleFavorite(pinRow));
    auto line = ugly::quip(wasPinned ? ugly::Quip::Unpin : ugly::Quip::Pin, 0, 0, 0, formRow(this, pinRow).question);
    { RenderLock lock(*this); form_.setQuip(pinRow, std::move(line)); }
    requestUpdate();
  } else {
    // An inert hold must leave the input-to-paint handshake open.
    if (pinRow >= 0) formPaintReady_.store(true);
    applyFormIntent(intent, homeEvent);
  }
  return true;
}


void StatusBarSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= visibleItemCount) return;
  if (shell::isUgly()) {
    ugly::QuestionSheet::Intent intent;
    {
      RenderLock lock(*this);
      focusForm(index);
      const auto row = formRow(this, index);
      intent = {ugly::QuestionSheet::IntentKind::Commit, row.id, index, (row.selected + 1) % row.count, true};
      formPaintReady_.store(false);
    }
    applyFormIntent(intent);
    return;
  }
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection() {
  const auto row = formRow(this, nav.selected);
  if (row.count) applyChosenValue(nav.selected, (row.selected + 1) % row.count, false);
}

bool StatusBarSettingsActivity::saveSettings(const bool repaint) {
  const bool saved = SETTINGS.saveToFile();
  saveFailed_.store(!saved);
  if (!saved) LOG_ERR("STATUS", "Saving status settings failed");
  if (repaint) requestUpdate();
  return saved;
}

bool StatusBarSettingsActivity::applyChosenValue(const int row, const int option, const bool repaint) {
  if (row < 0 || row >= visibleItemCount || option < 0 || option >= 2) return false;
  // Clock mode 0 is legacy. An explicit selection writes the visible layout.
  if (formRow(this, row).selected == option &&
      !(itemAt(row) == ITEM_CLOCK && SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_HIDE)) return true;
  {
    RenderLock lock(*this);
    switch (itemAt(row)) {
      case ITEM_TITLE:
        SETTINGS.statusBarTitle = option ? CrossPointSettings::CHAPTER_TITLE : CrossPointSettings::HIDE_TITLE;
        break;
      case ITEM_CHAPTER_PAGE_COUNT: SETTINGS.statusBarChapterPageCount = option; break;
      case ITEM_BOOK_PROGRESS_PERCENTAGE: SETTINGS.statusBarBookProgressPercentage = option; break;
      case ITEM_CLOCK:
        SETTINGS.statusBarClock = option ? CrossPointSettings::STATUS_BAR_CLOCK_LEFT
                                        : CrossPointSettings::STATUS_BAR_CLOCK_RIGHT;
        break;
      default: return false;
    }
  }
  saveSettings(repaint);
  return true;
}

ugly::QuestionSheet::Row StatusBarSettingsActivity::formRow(void* context, const int row) {
  const auto& self = *static_cast<StatusBarSettingsActivity*>(context);
  if (row < 0 || row >= self.visibleItemCount) return {};
  ugly::QuestionSheet::Row out;
  out.id = static_cast<uint32_t>(TENOR_NAMES[row]) + 1;
  out.question = I18N.get(TENOR_NAMES[row]);
  out.kind = ugly::QuestionSheet::Kind::Toggle;
  out.count = 2;
  switch (itemAt(row)) {
    case ITEM_TITLE: out.selected = SETTINGS.statusBarTitle != CrossPointSettings::HIDE_TITLE; break;
    case ITEM_CHAPTER_PAGE_COUNT: out.selected = SETTINGS.statusBarChapterPageCount != 0; break;
    case ITEM_BOOK_PROGRESS_PERCENTAGE: out.selected = SETTINGS.statusBarBookProgressPercentage != 0; break;
    case ITEM_CLOCK:
      out.kind = ugly::QuestionSheet::Kind::Choice;
      out.selected = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
      break;
  }
  return out;
}

void StatusBarSettingsActivity::formLabel(void* context, const int row, const int option, char* out, const size_t size) {
  if (!size) return;
  out[0] = 0;
  if (option < 0 || option >= formRow(context, row).count) return;
  const char* label = itemAt(row) == ITEM_CLOCK
      ? (option ? tr(STR_CLOCK_LEFT_BATTERY_RIGHT) : tr(STR_BATTERY_LEFT_CLOCK_RIGHT))
      : (option ? tr(STR_SHOW) : tr(STR_HIDE));
  snprintf(out, size, "%s", label);
}

void StatusBarSettingsActivity::focusForm(const int row) {
  if (row < 0 || row >= visibleItemCount) return;
  const int previousSheet = form_.sheet();
  for (int n = 0; form_.question() != row && n < visibleItemCount; ++n)
    form_.input(ugly::QuestionSheet::Key::NextQuestion);
  if (previousSheet != form_.sheet()) ++formSurface_;
  activeNav().selected = row;
}

void StatusBarSettingsActivity::bindForm() {
  ++formSurface_;
  ugly::QuestionSheet::View view;
  view.context = this;
  view.count = visibleItemCount;
  view.subject = tr(STR_HIDE_GLOBAL_STATUS_BAR);
  view.date = ReadingStatsStore::currentDay();
  view.code = 11;
  view.row = formRow;
  view.label = formLabel;
  form_.bind(renderer, view, mappedInput.hasTouch());
  focusForm(std::clamp(static_cast<int>(activeNav().selected), 0, std::max(0, visibleItemCount - 1)));
  formPaintReady_.store(false);
}

void StatusBarSettingsActivity::prepareFormQuip(const int row, const int candidate) {
  if (row < 0 || row >= visibleItemCount || candidate < 0 || candidate >= 2) return;
  const StrId label = itemAt(row) == ITEM_CLOCK
      ? (candidate ? StrId::STR_CLOCK_LEFT_BATTERY_RIGHT : StrId::STR_BATTERY_LEFT_CLOCK_RIGHT)
      : (candidate ? StrId::STR_SHOW : StrId::STR_HIDE);
  auto line = ugly::quip(ugly::Quip::SetValue,
      ugly::logic::quipKey(I18N.get(TENOR_NAMES[row], Language::VI), I18N.get(label, Language::VI)), 0, candidate);
  RenderLock lock(*this);
  form_.setQuip(row, std::move(line));
}

void StatusBarSettingsActivity::applyFormIntent(const ugly::QuestionSheet::Intent& intent, const bool home) {
  using Intent = ugly::QuestionSheet::IntentKind;
  if (intent.kind == Intent::Back) {
    formCount_ = 0;
    if (saveFailed_.load() && !saveSettings(false)) { requestUpdate(); return; }
    if (home) onGoHome(HomeMenuItem::SETTINGS_MENU);
    else finish();
    return;
  }
  if (intent.row >= 0 && intent.row < visibleItemCount && intent.id == formRow(this, intent.row).id) {
    if (intent.kind == Intent::Commit) {
      const int previous = formRow(this, intent.row).selected;
      if (applyChosenValue(intent.row, intent.candidate, false)) {
        RenderLock lock(*this);
        form_.didCommit(intent.row, previous);
      }
    }
    if (intent.kind == Intent::Preview || intent.kind == Intent::Commit) prepareFormQuip(intent.row, intent.candidate);
  }
  if (intent.repaint) requestUpdate();
}

void StatusBarSettingsActivity::onPause() {
  form_.invalidate();
  formPaintReady_.store(false);
  formCount_ = 0;
}

void StatusBarSettingsActivity::onResume() {
  if (shell::isUgly()) bindForm();  // ActivityManager owns RenderLock.
}

bool StatusBarSettingsActivity::handleHomeGesture() {
  if (!shell::isUgly()) return false;
  queueForm({FormEvent::Type::Key, ugly::QuestionSheet::Key::Home});
  return true;
}

void StatusBarSettingsActivity::restoreNavigation(const MenuNavigationState& state) {
  UiListActivity::restoreNavigation(state);
  if (shell::isUgly()) {
    RenderLock lock(*this);
    focusForm(activeNav().selected);
  }
}

int StatusBarSettingsActivity::focusFavorite(const std::string& key) {
  const int row = UiListActivity::focusFavorite(key);
  if (shell::isUgly() && row >= 0) {
    RenderLock lock(*this);
    focusForm(row);
  }
  return row;
}

int StatusBarSettingsActivity::favoriteSelectedRow() {
  return shell::isUgly() ? form_.question() : UiListActivity::favoriteSelectedRow();
}

bool StatusBarSettingsActivity::handleButtons() {
  if (backReleased()) {
    if (!saveFailed_.load() || saveSettings()) finish();
    return true;
  }
  if (confirmReleased()) {
    activateIndex(activeNav().selected);
    return true;
  }
  return false;
}

std::string StatusBarSettingsActivity::rowValueText(const int index) {
  if (index < 0 || index >= visibleItemCount) return {};
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
  if (shell::isUgly()) {
    [[maybe_unused]] const uint32_t started = millis();
    form_.paint(renderer, mappedInput);
    if (saveFailed_.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
    else if (formPinFailed_.load()) GUI.drawPopup(renderer, tr(STR_MENU_SAVE_FAILED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    formVisibleSurface_.store(formSurface_);
    formPaintReady_.store(true);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "Status form total=%lums rows=%d sheet=%d/%d question=%d candidate=%d paper=%d",
            static_cast<unsigned long>(millis() - started), visibleItemCount, form_.sheet() + 1, form_.sheetCount(),
            form_.question(), form_.candidate(), form_.paperOpen());
#endif
    return;
  }
  if (optionPopup.processRender(renderer, mappedInput)) return;

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the list renders through the app.
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawNavigationHeader(tr(STR_CUSTOMISE_STATUS_BAR));
    renderUi();
  });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (saveFailed_.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
  renderer.displayBuffer();
}

std::string StatusBarSettingsActivity::favoriteKey(int row) const {
  // Tenor pins only the corner row: the favorites catalog labels every other
  // status pin as empty there, so a pinned switch would show no name.
  if (row < 0 || row >= visibleItemCount || itemAt(row) != ITEM_CLOCK) return {};
  return menufavorites::keyFor("status", 0, itemAt(row));
}
