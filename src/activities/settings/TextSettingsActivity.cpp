#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "SettingsList.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyQuip.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderInkWeight.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#include "components/TenorMenuChrome.h"
#include "components/SettledListRender.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Tab labels for Font | Size | Layout | Style.
constexpr StrId TAB_NAME_IDS[] = {StrId::STR_FONT, StrId::STR_SIZE, StrId::STR_LAYOUT, StrId::STR_STYLE};

// Same order as TextSettingsActivity::LayoutRow, one label per row.
constexpr StrId LAYOUT_ROW_NAME_IDS[] = {StrId::STR_LINE_SPACING,     StrId::STR_LETTER_SPACING,
                                         StrId::STR_WORD_SPACING,     StrId::STR_EXTRA_SPACING,
                                         StrId::STR_PARA_ALIGNMENT,   StrId::STR_SCREEN_MARGIN,
                                         StrId::STR_PARAGRAPH_INDENT};
constexpr StrId STYLE_ROW_NAME_IDS[] = {StrId::STR_FOCUS_READING, StrId::STR_HYPHENATION, StrId::STR_EMBEDDED_STYLE,
                                        StrId::STR_TEXT_AA, StrId::STR_READER_INK_WEIGHT};

// One label set for every spacing row: line, letter and paragraph spacing share
// one five-value scale (readerSpacing::Level), so they share one label order.
// No percent/pixel-offset word or character spacing labels: upstream's #3528
// redesign is not taken (see RESOLUTION.md).
constexpr StrId SPACING_LEVEL_IDS[] = {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT,
                                       StrId::STR_WIDE, StrId::STR_VERY_WIDE};
constexpr StrId INK_WEIGHT_IDS[] = {StrId::STR_READER_INK_0, StrId::STR_READER_INK_1,
                                    StrId::STR_READER_INK_2, StrId::STR_READER_INK_3};
static_assert(std::size(INK_WEIGHT_IDS) == readerInk::LEVEL_COUNT, "reader ink labels");
// Tắt / Mặc định / Lớn, matching readerSpacing::DropCapMode.
constexpr StrId DROP_CAP_IDS[] = {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_SPACING_LARGE};
static_assert(std::size(DROP_CAP_IDS) == readerSpacing::DROP_CAP_MODE_COUNT, "drop cap labels");
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr StrId INDENT_IDS[] = {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_WIDE};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
constexpr int MARGIN_STEP = CrossPointSettings::SCREEN_MARGIN_STEP;
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab)
    : UiTabListActivity("TextSettings", renderer, mappedInput), registry_(registry), tab_(initialTab) {}

const char* TextSettingsActivity::tabLabel(const int index) const { return I18N.get(TAB_NAME_IDS[index]); }

void TextSettingsActivity::onEnter() {
  RenderLock lock(*this);
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
#endif
  UiTabListActivity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = tenorchrome::enabled() ? tenorchrome::contentTopUnderTabs()
                                       : metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  updatePreviewGeometry();

  // Danh sach ho va ho dang dung lay tu fontdoc, cung mot cho voi menu doc va giu nut.
  fonts_.clear();
  for (const auto& h : fontdoc::danhSachHo(registry_)) fonts_.push_back({h.ten, h.builtin, h.chiSo});

  rebuildSizeList();

  currentFamilyIndex_ = fontdoc::hoDangDung(registry_);
  for (auto& n : tabNavs) n.selected = 1;  // default to the first list row
  tabNavs[static_cast<int>(Tab::Family)].selected = currentFamilyIndex_ + 1;
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;

  if (shell::isUgly()) {
    ugly::ensureFonts(renderer);
    bindForm();
    focusForm(formIndex(tab_, 0));
  } else rebuildRowItems();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("TXT", "Enter ms=%lu", millis() - started);
#endif
}

// Rebuilds rowItems_ (label + actionValue) for the active tab. Structural -
// call only when tab_ or its backing data (fonts_/sizes_) changes, never from
// buildScreen(), which just refreshes rowValues_/rowItems_[].value in place.
void TextSettingsActivity::rebuildRowItems() {
  if (shell::isUgly()) return;
  const int count = listCount();
  rowValues_.assign(count, std::string());
  rowItems_.clear();
  rowItems_.reserve(count);
  for (int i = 0; i < count; i++) {
    fui::ListItem item;
    switch (tab_) {
      case Tab::Family:
        item.label = fonts_[i].name.c_str();
        break;
      case Tab::Size:
        item.label = sizes_[i].name.c_str();
        break;
      case Tab::Layout:
        item.label = I18N.get(LAYOUT_ROW_NAME_IDS[i]);
        break;
      case Tab::Style:
        item.label = I18N.get(STYLE_ROW_NAME_IDS[i]);
        break;
      default:
        break;
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

// The selectable sizes belong to the active family, so this runs on entry and
// again after every family change. A family change goes through ensureLoaded(),
// which snaps SETTINGS.fontPointSize into the new family's set - but entry does
// not, so the highlight is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);

  // The stored size can still sit outside this family's set - e.g. the family
  // was deleted while selected, or the card was swapped. Highlight the size the
  // reader actually renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    // "pt" is deliberately not translated: it is the typographic unit symbol,
    // written the same way in every language CrossPoint ships.
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

void TextSettingsActivity::onTabAction(const int index) {
  if (index < 0 || index >= tabCount()) return;
  if (shell::isUgly()) {
    RenderLock lock(*this);
    tab_ = static_cast<Tab>(index);
    return;
  }
  if (optionPopup_.isActive()) return;
  if (tab_ != static_cast<Tab>(index)) {
    RenderLock lock(*this);
    tab_ = static_cast<Tab>(index);
    rebuildRowItems();
    auto& n = activeNav();
    if (!mappedInput.hasTouch() && listCount() > 0 && n.selected <= 0) n.selected = 1;
    n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
    requestUpdate();
  }
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void TextSettingsActivity::activateIndex(const int index) {
  if (shell::isUgly()) {
    if (index < 0 || index >= 14) return;
    ugly::QuestionSheet::Intent intent;
    {
      RenderLock lock(*this);
      focusForm(index);
      const auto row = formRow(this, index);
      if (index < 2) {
        // Font/Size favorites open their chooser with the current value intact.
        if (row.kind == ugly::QuestionSheet::Kind::Paper && !form_.paperOpen()) {
          intent = form_.input(ugly::QuestionSheet::Key::Confirm);
          ++formSurface_;
        } else intent.repaint = true;
      } else {
        // Layout/Style favorite launches retain their direct cycle behavior.
        intent = {ugly::QuestionSheet::IntentKind::Commit, row.id, index,
                  row.count ? (row.selected + 1) % row.count : 0, true};
      }
      formPaintReady_.store(false);
    }
    applyFormIntent(intent);
    return;
  }
  if (optionPopup_.isActive()) return;
  // Most rows repaint a different surface (popup, preview, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  activateRow(index);
}

void TextSettingsActivity::queueForm(FormEvent event) {
  if (event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold)
    event.surface = formVisibleSurface_.load();
  if (formCount_ < formQueue_.size()) formQueue_[(formHead_ + formCount_++) % formQueue_.size()] = event;
  else LOG_ERR("UGLY", "Settings form queue full");
}

void TextSettingsActivity::pollTilt() {
  if (!shell::isUgly()) UiTabListActivity::pollTilt();
}

bool TextSettingsActivity::handleCustomInput() {
  if (!shell::isUgly()) return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
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
    activeNav().selected = form_.question() + 1;
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


int TextSettingsActivity::formIndex(const Tab tab, const int row) {
  if (tab == Tab::Family) return 0;
  if (tab == Tab::Size) return 1;
  if (tab == Tab::Layout && row >= 0 && row < static_cast<int>(LayoutRow::Count)) return row + 2;
  if (tab == Tab::Style && row >= 0 && row < static_cast<int>(StyleRow::Count)) return row + 9;
  return -1;
}

TextSettingsActivity::Tab TextSettingsActivity::formTab(const int row) {
  return row == 0 ? Tab::Family : row == 1 ? Tab::Size : row < 9 ? Tab::Layout : Tab::Style;
}

int TextSettingsActivity::formLocalRow(const int row) {
  return row < 2 ? 0 : row < 9 ? row - 2 : row - 9;
}

const SettingInfo* TextSettingsActivity::formSetting(const int row) {
  if (row < 0 || row >= 14) return nullptr;
  const StrId name = row == 0 ? StrId::STR_FONT_FAMILY : row == 1 ? StrId::STR_FONT_SIZE
                     : row < 9 ? LAYOUT_ROW_NAME_IDS[row - 2] : STYLE_ROW_NAME_IDS[row - 9];
  for (const auto& setting : getBaseSettingsList()) if (setting.nameId == name) return &setting;
  return nullptr;
}

ugly::QuestionSheet::Row TextSettingsActivity::formRow(void* context, const int row) {
  using Kind = ugly::QuestionSheet::Kind;
  auto& self = *static_cast<TextSettingsActivity*>(context);
  const auto* setting = formSetting(row);
  if (!setting) return {};
  ugly::QuestionSheet::Row out;
  out.id = static_cast<uint32_t>(setting->nameId) + 1;
  out.question = I18N.get(setting->nameId);
  if (row == 0) {
    out.count = static_cast<int>(self.fonts_.size());
    out.selected = self.currentFamilyIndex_;
    out.kind = out.count >= 7 ? Kind::Paper : Kind::Choice;
  } else if (row == 1) {
    out.count = static_cast<int>(self.sizes_.size());
    out.selected = self.currentSizeIndex_;
    out.kind = out.count <= 6 ? Kind::Choice : out.count <= 15 ? Kind::Ruler : Kind::Paper;
  } else {
    int value = setting->valuePtr ? SETTINGS.*setting->valuePtr : 0;
    if (setting->nameId == StrId::STR_READER_INK_WEIGHT) value = readerInk::clamp(value);
    if (setting->nameId == StrId::STR_FOCUS_READING) value = readerSpacing::clampDropCapMode(value);
    if (setting->type == SettingType::TOGGLE) {
      out.count = 2;
      out.selected = value != 0;
      out.kind = Kind::Toggle;
    } else if (setting->type == SettingType::VALUE && setting->valueRange.step) {
      out.count = (setting->valueRange.max - setting->valueRange.min) / setting->valueRange.step + 1;
      out.selected = (value - setting->valueRange.min) / setting->valueRange.step;
      out.kind = out.count <= 6 ? Kind::Choice : out.count <= 11 ? Kind::Ruler : Kind::Paper;
    } else {
      out.count = static_cast<int>(setting->enumLabels().size());
      out.selected = value;
      out.kind = out.count >= 7 ? Kind::Paper : Kind::Choice;
    }
  }
  out.selected = std::clamp(out.selected, 0, std::max(0, out.count - 1));
  return out;
}

void TextSettingsActivity::formLabel(void* context, const int row, const int option, char* out, const size_t size) {
  if (!size) return;
  out[0] = 0;
  auto& self = *static_cast<TextSettingsActivity*>(context);
  const auto* setting = formSetting(row);
  const auto info = formRow(context, row);
  if (!setting || option < 0 || option >= info.count) return;
  const char* label = "";
  if (row == 0) label = self.fonts_[option].name.c_str();
  else if (row == 1) label = self.sizes_[option].name.c_str();
  else if (setting->type == SettingType::TOGGLE) label = option ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  else if (setting->type == SettingType::VALUE) {
    snprintf(out, size, "%u", static_cast<unsigned>(setting->valueRange.min + option * setting->valueRange.step));
    return;
  } else label = I18N.get(setting->enumLabels()[option]);
  snprintf(out, size, "%s", label);
}

bool TextSettingsActivity::saveSettings(const bool repaint) {
  const bool saved = SETTINGS.saveToFile();
  saveFailed_.store(!saved);
  if (!saved) LOG_ERR("TXT", "Saving text settings failed");
  if (repaint) requestUpdate();
  return saved;
}

bool TextSettingsActivity::applyChosenValue(const Tab tab, const int row, const int option, const bool repaint) {
  const int question = formIndex(tab, row);
  if (question < 0) return false;
  const auto current = formRow(this, question);
  if (option < 0 || option >= current.count) return false;
  if (current.selected == option) return true;
  if (tab == Tab::Family) {
    if (!applyFamily(option)) return false;
  } else if (tab == Tab::Size) {
    if (!applySize(option)) return false;
  } else {
    const auto* setting = formSetting(question);
    if (!setting || !setting->valuePtr) return false;
    RenderLock lock(*this);
    SETTINGS.*setting->valuePtr = static_cast<uint8_t>(setting->type == SettingType::VALUE
        ? setting->valueRange.min + option * setting->valueRange.step : option);
    if (setting->valuePtr == &CrossPointSettings::readerInkWeight) sdFontSystem.ensureLoaded(renderer);
  }
  // RAM stays applied on an SD failure. Its visible error and Back retry are
  // separate from the font lifecycle, whose resources changed under the lock.
  saveSettings(repaint);
  return true;
}

void TextSettingsActivity::focusForm(const int row) {
  if (row < 0 || row >= 14) return;
  const int previousSheet = form_.sheet();
  for (int n = 0; form_.question() != row && n < 14; ++n) form_.input(ugly::QuestionSheet::Key::NextQuestion);
  if (previousSheet != form_.sheet()) ++formSurface_;
  activeNav().selected = row + 1;
}

void TextSettingsActivity::bindForm() {
  ++formSurface_;
  ugly::QuestionSheet::View view;
  view.context = this;
  view.count = 14;
  view.subject = tr(STR_TEXT_SETTINGS);
  view.date = ReadingStatsStore::currentDay();
  view.code = 10;
  view.row = formRow;
  view.label = formLabel;
  form_.bind(renderer, view, mappedInput.hasTouch());
  activeNav().selected = form_.question() + 1;
  formPaintReady_.store(false);
}

void TextSettingsActivity::prepareFormQuip(const int row, const int candidate) {
  if (row < 0 || row >= 14 || candidate < 0 || candidate >= formRow(this, row).count) return;
  uint16_t key = 0;
  int number = candidate;
  if (row == 0) key = ugly::valueKey(buildFontFamilySetting(registry_), candidate);
  else if (row == 1) {
    key = ugly::valueKey(buildFontSizeSetting(registry_), candidate);
    number = sizes_[candidate].pointSize;
  } else {
    const auto* setting = formSetting(row);
    if (setting->type == SettingType::VALUE) number = setting->valueRange.min + candidate * setting->valueRange.step;
    key = ugly::valueKey(*setting, number);
  }
  auto line = ugly::quip(ugly::Quip::SetValue, key, 0, number);
  if (row == 13 && !readerInk::available(candidate, sdFontSystem.availableWeightMask())) {
    if (!line.empty()) line += '\n';
    line += tr(STR_INK_UNAVAILABLE);
  }
  RenderLock lock(*this);
  form_.setQuip(row, std::move(line));
}

void TextSettingsActivity::applyFormIntent(const ugly::QuestionSheet::Intent& intent, const bool home) {
  using Intent = ugly::QuestionSheet::IntentKind;
  if (intent.kind == Intent::Back) {
    formCount_ = 0;
    if (saveFailed_.load() && !saveSettings(false)) { requestUpdate(); return; }
    if (home) onGoHome(HomeMenuItem::SETTINGS_MENU);
    else finish();
    return;
  }
  if (intent.row >= 0 && intent.row < 14 && intent.id == formRow(this, intent.row).id) {
    if (intent.kind == Intent::Commit) {
      const int previous = formRow(this, intent.row).selected;
      if (applyChosenValue(formTab(intent.row), formLocalRow(intent.row), intent.candidate, false)) {
        RenderLock lock(*this);
        form_.didCommit(intent.row, previous);
      }
    }
    if (intent.kind == Intent::Preview || intent.kind == Intent::Commit) prepareFormQuip(intent.row, intent.candidate);
  }
  if (intent.repaint) requestUpdate();
}

void TextSettingsActivity::onPause() {
  form_.invalidate();
  formPaintReady_.store(false);
  formCount_ = 0;
}

void TextSettingsActivity::onResume() {
  if (shell::isUgly()) bindForm();  // ActivityManager owns RenderLock.
}

bool TextSettingsActivity::handleHomeGesture() {
  if (!shell::isUgly()) return false;
  queueForm({FormEvent::Type::Key, ugly::QuestionSheet::Key::Home});
  return true;
}

void TextSettingsActivity::restoreNavigation(const MenuNavigationState& state) {
  UiTabListActivity::restoreNavigation(state);
  if (shell::isUgly()) {
    RenderLock lock(*this);
    focusForm(std::clamp(ringPos() - 1, 0, 13));
  }
}

int TextSettingsActivity::favoriteSelectedRow() {
  return shell::isUgly() ? form_.question() : UiTabListActivity::favoriteSelectedRow();
}

bool TextSettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Roi man mot nhip (chot 14/09/2026 dem), nhu menu doc va man Cai dat.
    if (!saveFailed_.load() || saveSettings()) finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      moveRingTo(1);  // cung luat voi ba man the kia: Chon o thanh the buoc xuong dong dau
    } else {
      activateRow(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void TextSettingsActivity::buildScreen(UiScreen& screen) {
  // Content sits below the preview pane (render() draws header + preview
  // directly) and above the caption band + button hints.
  const int tabTop = tenorchrome::enabled() ? tenorchrome::tabTop() : afterHeader + previewHeight;
  const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(tabTop), 0, static_cast<int16_t>(bottomReserved + captionHeight), 0});

  buildTabBar(screen);
  if (tenorchrome::enabled()) screen.takeTop(static_cast<int16_t>(previewHeight));

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the tab
  // was last switched; only the live value text needs refreshing here, by
  // assigning into the existing rowValues_ strings (no vector growth) rather
  // than building a new items/values vector on every render.
  const int count = listCount();
  for (int i = 0; i < count; i++) {
    switch (tab_) {
      case Tab::Family:
        rowValues_[i] = (i == currentFamilyIndex_ && !tenorchrome::kTouchShell) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Size:
        rowValues_[i] = (i == currentSizeIndex_ && !tenorchrome::kTouchShell) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Layout:
        rowValues_[i] = layoutValueText(i);
        break;
      case Tab::Style:
        rowValues_[i] = styleValueText(i);
        break;
      default:
        break;
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    // Touch: the font or size in use is the chosen row (bold, a tick), no word.
    rowItems_[i].chosen = tenorchrome::kTouchShell && ((tab_ == Tab::Family && i == currentFamilyIndex_) ||
                                                       (tab_ == Tab::Size && i == currentSizeIndex_));
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (see SettingsActivity).
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

const char* TextSettingsActivity::confirmLabelText() const {
  if (ringPos() == 0) return tr(STR_SELECT);
  switch (tab_) {
    case Tab::Layout:
      // Inline choices cycle directly; the other rows open a picker.
      return ringPos() - 1 == static_cast<int>(LayoutRow::LineSpacing) ||
                     ringPos() - 1 == static_cast<int>(LayoutRow::LetterSpacing) ||
                     ringPos() - 1 == static_cast<int>(LayoutRow::WordSpacing) ||
                     ringPos() - 1 == static_cast<int>(LayoutRow::ParaSpacing) ||
                     ringPos() - 1 == static_cast<int>(LayoutRow::ParaIndent)
                 ? tr(STR_TOGGLE)
                 : tr(STR_SELECT);
    case Tab::Style:
      return tr(STR_TOGGLE);
    default:
      return tr(STR_SELECT);
  }
}

void TextSettingsActivity::render(RenderLock&&) {
  if (shell::isUgly()) {
    [[maybe_unused]] const uint32_t started = millis();
    form_.paint(renderer, mappedInput);
    if (saveFailed_.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
    else if (formPinFailed_.load()) GUI.drawPopup(renderer, tr(STR_MENU_SAVE_FAILED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    formVisibleSurface_.store(formSurface_);
    formPaintReady_.store(true);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "Text form total=%lums rows=14 sheet=%d/%d question=%d candidate=%d paper=%d",
            static_cast<unsigned long>(millis() - started), form_.sheet() + 1, form_.sheetCount(), form_.question(),
            form_.candidate(), form_.paperOpen());
#endif
    return;
  }
  if (optionPopup_.processRender(renderer, mappedInput)) return;  // picker draws over everything

  updatePreviewGeometry();

  const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                               ? fonts_[currentFamilyIndex_].name.c_str()
                               : "";
  const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                             ? sizes_[currentSizeIndex_].name.c_str()
                             : "";
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
  unsigned long previewMs = 0;
  unsigned passes = 0;
#endif
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawNavigationHeader(tr(STR_TEXT_SETTINGS));
#ifdef TENOR_PRESS_PROBE
    const unsigned long previewStarted = millis();
    ++passes;
#endif
    textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing, afterHeader,
                               previewHeight, familyName, sizeName);
#ifdef TENOR_PRESS_PROBE
    previewMs += millis() - previewStarted;
#endif
    renderUi();
  });

  const bool weightUnavailable =
      tab_ == Tab::Style && ringPos() - 1 == static_cast<int>(StyleRow::InkWeight) &&
      !readerInk::available(SETTINGS.readerInkWeight, sdFontSystem.availableWeightMask());
  if (weightUnavailable || focusedRowHasNoPreview()) {
    const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
    const int capY = afterHeader + usableHeight - captionHeight + metrics_.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics_.previewPadding, capY,
                      weightUnavailable ? tr(STR_INK_UNAVAILABLE) : tr(STR_NOT_IN_PREVIEW));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabelText(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
#ifdef TENOR_PRESS_PROBE
  const unsigned long painted = millis();
#endif
  if (saveFailed_.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
  renderer.displayBuffer();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("TXT", "Frame paint=%lu preview=%lu passes=%u display=%lu", painted - started, previewMs, passes,
          millis() - painted);
#endif
}

void TextSettingsActivity::updatePreviewGeometry() {
  usableHeight = std::max(0, renderer.getScreenHeight() - afterHeader - bottomReserved);
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;

  const int lineAdvance = std::max(1, renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression()));
  const int lineHeight = std::max(0, renderer.getTextHeight(fontId));
  const int normalExtent = std::max(lineAdvance, lineHeight);
  const int dropCapHeight = readerSpacing::dropCapHeight(
      readerSpacing::clampDropCapMode(SETTINGS.dropCapMode), lineAdvance);
  const int firstParagraphExtent = std::max(
      static_cast<int>(textsettings::FIRST_PARAGRAPH_LINES) * lineAdvance,
      dropCapHeight > 0 ? dropCapHeight + 4 : 0);
  const int paragraphGap = readerSpacing::paragraphGap(SETTINGS.extraParagraphSpacing, lineAdvance);
  const int labelReserved =
      renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing + metrics_.previewPadding;
  const int needed = labelReserved + metrics_.previewPadding + firstParagraphExtent + paragraphGap + normalExtent;
  // Keep two settings rows reachable while allowing large spacing combinations
  // to retain both paragraphs in the preview.
  const int listReserved = 2 * (metrics_.listRowHeight + metrics_.listRowGap) +
                           renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing +
                           (tenorchrome::enabled() ? 0 : metrics_.tabBarHeight);
  const int maxPreviewHeight = std::max(0, usableHeight - listReserved);
  previewHeight = std::clamp(std::max(previewHeight, needed), 0, maxPreviewHeight);
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the
// next one, and the render task walks that same object inside the preview's
// prewarmCache() - so without this lock a font switch can free the mini glyph
// arrays out from under prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
bool TextSettingsActivity::applyFamily(const int listIndex) {
  RenderLock lock;
  if (!fontdoc::apHo(renderer, registry_, listIndex)) return false;
  currentFamilyIndex_ = listIndex;
  if (shell::isUgly()) form_.invalidate();
  rebuildSizeList();
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;
  if (shell::isUgly()) bindForm();
  return true;
}

void TextSettingsActivity::activateRow(const int row) {
  if (row < 0 || row >= listCount()) return;
  commitTabNavigation();
  if (tab_ == Tab::Family || tab_ == Tab::Size) applyChosenValue(tab_, 0, row);
  else if (tab_ == Tab::Layout) confirmLayoutRow(row);
  else if (tab_ == Tab::Style) confirmStyleRow(row);
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font
// file, which frees and replaces the SdCardFont the render task may be reading.
bool TextSettingsActivity::applySize(const int listIndex) {
  RenderLock lock;
  if (listIndex < 0 || listIndex >= static_cast<int>(sizes_.size())) return false;
  currentSizeIndex_ = listIndex;
  fontdoc::apCo(renderer, sizes_[listIndex].pointSize);
  return true;
}

void TextSettingsActivity::confirmLayoutRow(const int row) {
  if (row < 0 || row >= static_cast<int>(LayoutRow::Count)) return;
  const auto current = formRow(this, formIndex(Tab::Layout, row));
  if (row == static_cast<int>(LayoutRow::Alignment)) {
    optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                      current.selected, [this, row](int option) { applyChosenValue(Tab::Layout, row, option); });
    requestUpdate();
  } else if (row == static_cast<int>(LayoutRow::ScreenMargin)) {
    std::vector<std::string> options;
    options.reserve(current.count);
    for (int m = MARGIN_MIN; m <= MARGIN_MAX; m += MARGIN_STEP) options.push_back(std::to_string(m));
    optionPopup_.show(StrId::STR_SCREEN_MARGIN, options, current.selected,
                      [this, row](int option) { applyChosenValue(Tab::Layout, row, option); });
    requestUpdate();
  } else if (current.count) {
    applyChosenValue(Tab::Layout, row, (current.selected + 1) % current.count);
  }
}

std::string TextSettingsActivity::layoutValueText(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::LetterSpacing:
      return I18N.get(SPACING_LEVEL_IDS[readerSpacing::clampLevel(SETTINGS.letterSpacing)]);
    case LayoutRow::WordSpacing:
      return I18N.get(SPACING_LEVEL_IDS[readerSpacing::clampLevel(SETTINGS.wordSpacing)]);
    case LayoutRow::ParaIndent:
      return I18N.get(INDENT_IDS[SETTINGS.paragraphIndent < std::size(INDENT_IDS) ? SETTINGS.paragraphIndent : 0]);
    case LayoutRow::LineSpacing:
      return I18N.get(SPACING_LEVEL_IDS[readerSpacing::clampLevel(SETTINGS.lineSpacing)]);
    case LayoutRow::ParaSpacing:
      return I18N.get(SPACING_LEVEL_IDS[readerSpacing::clampLevel(SETTINGS.extraParagraphSpacing)]);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);

    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(const int row) {
  const auto current = formRow(this, formIndex(Tab::Style, row));
  if (row >= 0 && row < static_cast<int>(StyleRow::Count) && current.count)
    applyChosenValue(Tab::Style, row, (current.selected + 1) % current.count);
}

std::string TextSettingsActivity::styleValueText(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::InkWeight:
      return I18N.get(INK_WEIGHT_IDS[readerInk::clamp(SETTINGS.readerInkWeight)]);
    case StyleRow::FocusReading:
      return I18N.get(DROP_CAP_IDS[readerSpacing::clampDropCapMode(SETTINGS.dropCapMode)]);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

// The chapter drop cap is visible in the preview; these book-specific settings are not.
bool TextSettingsActivity::focusedRowHasNoPreview() const {
  if (ringPos() == 0 || tab_ != Tab::Style) return false;
  const StyleRow row = static_cast<StyleRow>(ringPos() - 1);
  return row == StyleRow::Hyphenation || row == StyleRow::EmbeddedStyle || row == StyleRow::AntiAliasing;
}

void TextSettingsActivity::switchTab(const int direction) {
  RenderLock lock(*this);
  tab_ = static_cast<Tab>(adjacentTab(direction));
  rebuildRowItems();
  auto& n = activeNav();
  if (!mappedInput.hasTouch() && listCount() > 0 && n.selected <= 0) n.selected = 1;
  n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
  requestUpdate();
}

int TextSettingsActivity::listCount() const {
  if (shell::isUgly()) return 14;
  switch (tab_) {
    case Tab::Family:
      return static_cast<int>(fonts_.size());
    case Tab::Size:
      return static_cast<int>(sizes_.size());
    case Tab::Layout:
      return static_cast<int>(LayoutRow::Count);
    case Tab::Style:
      return static_cast<int>(StyleRow::Count);

    default:
      return 0;
  }
}

std::string TextSettingsActivity::favoriteKey(int row) const {
  if (shell::isUgly()) {
    if (row < 0 || row >= 14) return {};
    return menufavorites::keyFor("text", static_cast<int>(formTab(row)), formLocalRow(row));
  }
  if (row < 0 || row >= listCount()) return {};
  return menufavorites::keyFor("text", activeTab(), tab_ == Tab::Family || tab_ == Tab::Size ? 0 : row);
}
int TextSettingsActivity::focusFavorite(const std::string& key) {
  const auto* route = menufavorites::find(key);
  if (!route || std::string(route->screen) != "text") return -1;
  if (shell::isUgly()) {
    const int question = formIndex(static_cast<Tab>(route->tab), route->row);
    RenderLock lock(*this);
    focusForm(question);
    return question;
  }
  onTabAction(route->tab);
  if (tab_ == Tab::Family || tab_ == Tab::Size) {
    RenderLock lock(*this);
    activeNav().selected = (tab_ == Tab::Family ? currentFamilyIndex_ : currentSizeIndex_) + 1;
    activeNav().followOnBuild = true;
    return ringPos() - 1;
  }
  for (int row = 0; row < listCount(); ++row)
    if (favoriteKey(row) == key) {
      RenderLock lock(*this);
      activeNav().selected = row + 1;
      activeNav().followOnBuild = true;
      return row;
    }
  return -1;
}
