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
#include "MenuTiltInput.h"
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
constexpr StrId TENOR_NAMES[] = {StrId::STR_READER_STATUS_TOP, StrId::STR_READER_STATUS_LEFT,
                                 StrId::STR_READER_STATUS_CENTER, StrId::STR_READER_STATUS_RIGHT};
constexpr StrId TOP_VALUES[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_STATE_OFF};
constexpr StrId SLOT_VALUES[] = {StrId::STR_STATE_OFF, StrId::STR_CLOCK, StrId::STR_BATTERY,
                                  StrId::STR_CHAPTER_PAGE_COUNT, StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                  StrId::STR_READER_STATUS_CHAPTER_ETA, StrId::STR_READER_STATUS_BOOK_ETA};
constexpr StrId CENTER_VALUES[] = {StrId::STR_STATE_OFF, StrId::STR_CLOCK, StrId::STR_BATTERY,
                                  StrId::STR_CHAPTER_PAGE_COUNT, StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                  StrId::STR_READER_STATUS_CHAPTER_ETA, StrId::STR_READER_STATUS_BOOK_ETA,
                                  StrId::STR_CHAPTER_NAME};
static_assert(std::size(CENTER_VALUES) == CrossPointSettings::STATUS_SLOT_COUNT);
constexpr int TENOR_ROW_COUNT = sizeof(TENOR_NAMES) / sizeof(TENOR_NAMES[0]);
static_assert(TENOR_ROW_COUNT == StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS,
              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with TENOR_ROWS");

const StrId* valuesAt(const int row) { return row == 0 ? TOP_VALUES : row == 2 ? CENTER_VALUES : SLOT_VALUES; }
int valueCount(const int row) { return row == 0 ? std::size(TOP_VALUES) : row == 2 ? std::size(CENTER_VALUES) : std::size(SLOT_VALUES); }
}  // namespace

StatusBarSettingsActivity::StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

void StatusBarSettingsActivity::onEnter() {
  RenderLock lock(*this);
  choiceRow_ = -1;
  UiListActivity::onEnter();

  visibleItemCount = TENOR_ROW_COUNT;
  // A mode picked in the reader menu or on the web shows what its name says; the
  // switches take that as their starting point before any of them is flipped.
  SETTINGS.adoptReaderStatusItems();

  // Labels never change (unlike the values, which track live SETTINGS
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < visibleItemCount; i++) {
    rowItems_[i].label = I18N.get(TENOR_NAMES[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
    rowItems_[i].opensNext = !tenorchrome::kTouchShell ||
        settingsChoiceStyle(valueCount(i), true) == SettingsChoiceStyle::Page;
  }
  if (shell::isUgly()) {
    ugly::ensureFonts(renderer);
    bindForm();
  }
}

void StatusBarSettingsActivity::queueForm(FormEvent event) {
  if (event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold || event.type == FormEvent::Type::Strike)
    event.surface = formVisibleSurface_.load();
  if (formCount_ < formQueue_.size()) formQueue_[(formHead_ + formCount_++) % formQueue_.size()] = event;
  else LOG_ERR("UGLY", "Settings form queue full");
}

void StatusBarSettingsActivity::pollTilt() {
  if (!shell::isUgly()) {
    UiListActivity::pollTilt();
    return;
  }
  menutilt::pollTabs(static_cast<uint8_t>(renderer.getOrientation()), false);
  const auto tilt = menutilt::pollRows(visibleItemCount > 0);
  using Key = ugly::QuestionSheet::Key;
  if (tilt.previous) queueForm({FormEvent::Type::Key, Key::PreviousQuestion});
  else if (tilt.next) queueForm({FormEvent::Type::Key, Key::NextQuestion});
}

bool StatusBarSettingsActivity::handleCustomInput() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (!shell::isUgly()) return false;
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
#if FREEINK_DEVICE_X4PRO
  int16_t sx = 0, sy = 0;
  if (mappedInput.wasStrike(sx, sy)) queueForm({FormEvent::Type::Strike, Key::Confirm, sx, sy});  // back to its default
#endif
  const auto swipe = mappedInput.wasSwipe();
  if (!back && !home) {
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) key(Key::NextSheet);
    else if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) key(Key::PreviousSheet);
  }
  if (!formCount_ || !formPaintReady_.load()) return true;
  ugly::QuestionSheet::Intent intent;
  int pinRow = -1, strikeRow = -1;
  bool homeEvent = false;
  {
    RenderLock lock(RenderLock::TryTake{});
    if (!lock.acquired() || !formPaintReady_.load()) return true;
    const auto event = formQueue_[formHead_];
    formHead_ = static_cast<uint8_t>((formHead_ + 1) % formQueue_.size());
    --formCount_;
    // A finger event belongs to the surface that was visible when sampled.
    // A tap queued before a paper opened cannot choose a row on that paper.
    if ((event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold || event.type == FormEvent::Type::Strike) &&
        event.surface != formSurface_)
      return true;
    homeEvent = event.type == FormEvent::Type::Key && event.key == Key::Home;
    const bool paperWasOpen = form_.paperOpen();
    const int previousSheet = form_.sheet();
    const int previousPaperFirst = form_.paperFirst();
    if (event.type == FormEvent::Type::Key) intent = form_.input(event.key);
    else if (event.type == FormEvent::Type::Tap) intent = form_.tap(event.x, event.y);
    else if (event.type == FormEvent::Type::Strike) strikeRow = form_.paperOpen() ? -1 : form_.questionAt(event.x, event.y);
    else if (!form_.paperOpen()) pinRow = event.type == FormEvent::Type::Hold ? form_.questionAt(event.x, event.y) : form_.question();
    if (paperWasOpen != form_.paperOpen() || previousSheet != form_.sheet() ||
        (form_.paperOpen() && previousPaperFirst != form_.paperFirst())) ++formSurface_;
    activeNav().selected = form_.question();
    if (intent.repaint || pinRow >= 0 || strikeRow >= 0) formPaintReady_.store(false);
  }
  if (strikeRow >= 0) {
    const int option = defaultOption(strikeRow), previous = formRow(this, strikeRow).selected;
    if (option >= 0 && applyChosenValue(strikeRow, option, false)) {
      RenderLock lock(*this);
      form_.didCommit(strikeRow, previous);
    }
    requestUpdate();
  } else if (pinRow >= 0 && !favoriteKey(pinRow).empty()) {
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
  if (choiceRow_ >= 0) {
    if (index >= 0 && index < valueCount(choiceRow_) && applyChosenValue(choiceRow_, index)) closeChoices();
    return;
  }
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
  nav.selected = index;
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection() {
  if (tenorchrome::kTouchShell) {
    const int row = nav.selected;
    showSettingsChoices(optionPopup_, TENOR_NAMES[row], valuesAt(row), valueCount(row), SETTINGS.readerStatusItem(row),
                        row, [this, row](const int selected) { applyChosenValue(row, selected); });
    return;
  }
  RenderLock lock(*this);
  const auto lines = rowFrameLines(rowFrameGap);
  const auto first = app.publishedRect(ACTION_ROW, 0);
  const auto last = app.publishedRect(ACTION_ROW, visibleItemCount - 1);
  choiceTop_ = first.y - lines.top;
  choiceBottom_ = last.y + last.height + lines.bottom;
  choiceRowHeight_ = first.height;
  choiceRow_ = nav.selected;
  choiceNav_.reset();
  choiceNav_.selected = SETTINGS.readerStatusItem(choiceRow_);
  resetUi();
}

int StatusBarSettingsActivity::listCount() const {
  return choiceRow_ < 0 ? visibleItemCount : valueCount(choiceRow_);
}

int StatusBarSettingsActivity::settingsChoiceCount(const int row) const {
  return choiceRow_ < 0 && row >= 0 && row < visibleItemCount ? valueCount(row) : 0;
}

std::string StatusBarSettingsActivity::navigationLabel() const {
  return I18N.get(choiceRow_ < 0 ? StrId::STR_CUSTOMISE_STATUS_BAR : TENOR_NAMES[choiceRow_]);
}

void StatusBarSettingsActivity::closeChoices() {
  RenderLock lock(*this);
  choiceRow_ = -1;
  resetUi();
  requestUpdate();
}

void StatusBarSettingsActivity::onBackButton() {
  if (choiceRow_ >= 0) closeChoices();
  else if (!saveFailed_.load() || saveSettings()) finish();
}

bool StatusBarSettingsActivity::saveSettings(const bool repaint) {
  const bool saved = SETTINGS.saveToFile();
  saveFailed_.store(!saved);
  if (!saved) LOG_ERR("STATUS", "Saving status settings failed");
  if (repaint) requestUpdate();
  return saved;
}

int StatusBarSettingsActivity::defaultOption(const int row) const {
  if (row < 0 || row >= visibleItemCount) return -1;
  using S = CrossPointSettings;
  uint8_t S::* const fields[] = {&S::readerStatusTop, &S::readerStatusLeft,
                                  &S::readerStatusCenter, &S::readerStatusRight};
  return S::defaultOf(fields[row]);
}

bool StatusBarSettingsActivity::applyChosenValue(const int row, const int option, const bool repaint) {
  if (row < 0 || row >= visibleItemCount || option < 0 || option >= valueCount(row)) return false;
  if (formRow(this, row).selected == option && SETTINGS.statusBarSpec().slotsEnabled) return true;
  {
    RenderLock lock(*this);
    if (!SETTINGS.setReaderStatusItem(row, option)) return false;
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
  out.kind = ugly::QuestionSheet::Kind::Choice;
  out.count = valueCount(row);
  out.selected = SETTINGS.readerStatusItem(row);
  return out;
}

void StatusBarSettingsActivity::formLabel(void* context, const int row, const int option, char* out, const size_t size) {
  if (!size) return;
  out[0] = 0;
  if (option < 0 || option >= formRow(context, row).count) return;
  snprintf(out, size, "%s", I18N.get(valuesAt(row)[option]));
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
  if (row < 0 || row >= visibleItemCount || candidate < 0 || candidate >= valueCount(row)) return;
  const StrId label = valuesAt(row)[candidate];
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
    onBackButton();
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
  return I18N.get(valuesAt(index)[SETTINGS.readerStatusItem(index)]);
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool choices = choiceRow_ >= 0;
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(choices && tenorchrome::kTouchShell
                                                  ? renderer.getScreenHeight() - choiceBottom_ + rowFrameLines(rowFrameGap).bottom
                                                  : metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_'s labels/actionValue were set once in onEnter(); only the live
  // value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  if (choices) {
    for (int i = 0; i < valueCount(choiceRow_); ++i) {
      choiceItems_[i].label = I18N.get(valuesAt(choiceRow_)[i]);
      choiceItems_[i].actionValue = i;
      choiceItems_[i].chosen = i == SETTINGS.readerStatusItem(choiceRow_);
    }
  } else {
    for (int i = 0; i < visibleItemCount; i++) {
      rowValues_[i] = rowValueText(i);
      rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    }
  }

  fui::ListProps props;
  props.items = choices ? choiceItems_ : rowItems_;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;  // also the explicitly-set marker, see SettingsActivity
  if (choices && tenorchrome::kTouchShell) {
    props.rowInset = tenorchrome::FOOT_BACK_X;
    props.sidePadding = 16;
    props.rowHeight = choiceRowHeight_;
    props.rowGap = rowFrameGap;
    props.chosenMark = fui::bitmapFromIcon(icon_row_chosen_24);
    props.scrollIndicator = false;
    screen.takeTop(static_cast<int16_t>(rowFrameLines(rowFrameGap).top));
  }
  syncListViewport(screen, props);
  if (choices) props.partialTrailingRow = false;
  screen.list(props);
}

void StatusBarSettingsActivity::drawChoiceFrame() {
  if (choiceRow_ < 0 || !tenorchrome::kTouchShell) return;
  tenorchrome::drawPanel(renderer, choiceTop_, choiceBottom_ - choiceTop_);
  const auto lines = rowFrameLines(rowFrameGap);
  const int first = choiceNav_.top, end = std::min(listCount(), first + choiceNav_.pageRowsFor(listCount()));
  for (int i = first + 1; i < end; ++i) {
    const auto row = app.publishedRect(ACTION_ROW, i);
    if (row.height > 0)
      tenorchrome::drawRowRule(renderer, row.y - lines.rule, tenorchrome::FOOT_BACK_X + 16,
                              renderer.getScreenWidth() - tenorchrome::FOOT_BACK_X - 17);
  }
  if (first > 0 || end < listCount()) {
    const int height = choiceBottom_ - choiceTop_;
    const int rows = std::max(1, end - first);
    setPageScrollRegion(choiceTop_, choiceBottom_, listCount() * height / rows, first * height / rows);
    drawPageScrollbar();
  }
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;
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

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the list renders through the app.
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawNavigationHeader(navigationLabel().c_str());
    renderUi();
    drawChoiceFrame();
  });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (saveFailed_.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
  renderer.displayBuffer();
}

std::string StatusBarSettingsActivity::favoriteKey(int row) const {
  // Tenor pins only the corner row: the favorites catalog labels every other
  // status pin as empty there, so a pinned switch would show no name.
  if (choiceRow_ >= 0 || row < 0 || row >= visibleItemCount) return {};
  return menufavorites::keyFor("status", 0, row == 1 ? 7 : 100 + row);
}
