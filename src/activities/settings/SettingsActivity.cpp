#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>

#include "ReadingStatsStore.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyQuip.h"

#include "AboutActivity.h"
#include "BlePageTurnerActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "HomeButtonSettingsActivity.h"
#include "InfoUpdateActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyShell.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "PanelChip.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "UIFontTiers.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/SettledListRender.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int theBanDau,
                                   const bool fromHomeGroup)
    : UiTabListActivity("Settings", renderer, mappedInput, /*wantsTouchLongPress=*/tenorchrome::kTouchShell),
      fromHomeGroup(fromHomeGroup),
      theBanDau(theBanDau >= 0 && theBanDau < settingstabs::TAB_COUNT ? theBanDau : 0) {}

std::vector<SettingInfo>& SettingsActivity::danhSachCuaThe(const settingstabs::Tab tab) {
  switch (tab) {
    case settingstabs::Tab::SLEEP:
      return sleepSettings;
    case settingstabs::Tab::SCREEN:
      return displaySettings;
    case settingstabs::Tab::READER:
      return readerSettings;
    case settingstabs::Tab::CONTROLS:
      return controlsSettings;
    case settingstabs::Tab::SYSTEM:
      return systemSettings;
    case settingstabs::Tab::DEVICE:
      return deviceSettings;
    case settingstabs::Tab::OTHER:
      return otherSettings;
    case settingstabs::Tab::KEYBOARD:
      return keyboardSettings;
    case settingstabs::Tab::MOTION:
      return motionSettings;
  }
  return systemSettings;
}

int SettingsActivity::tabCount() const { return deviceSettingsTabCount(); }

bool SettingsActivity::listedAsRow(const SettingInfo& setting) {
  if (home_button::isSetting(setting.valuePtr)) return false;
  if (setting.valuePtr == &CrossPointSettings::uiUglyLevel) return ugly::logic::levelRowShown(SETTINGS.uiShell);
  return !(BoardConfig::hasHomeKey() && setting.nameId == StrId::STR_LONG_PRESS_MENU);
}

bool SettingsActivity::applyUiTextSize(GfxRenderer& renderer, const uint8_t size) {
  if (!applyUiFontSize(renderer, size)) return false;
  SETTINGS.uiTextSize = size;
  UITheme::getInstance().reload();
  return true;
}

void SettingsActivity::rebuildSettingsLists(const bool lockHeld) {
  std::optional<RenderLock> lock;
  if (!lockHeld) lock.emplace(*this);
  form_.invalidate();
  formPaintReady_.store(false);
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  deviceSettings.clear();
  otherSettings.clear();
  keyboardSettings.clear();
  sleepSettings.clear();
  motionSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran - otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  static constexpr struct {
    StrId nhan;
    SettingAction viec;
  } DONG_HANH_DONG[] = {
      {StrId::STR_LANGUAGE, SettingAction::Language},
      {StrId::STR_DEVICE_NAME, SettingAction::DeviceName},
      {StrId::STR_WIFI_NETWORKS, SettingAction::Network},
      {StrId::STR_BLE_PAGE_TURNER, SettingAction::BlePageTurner},
      {StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync},
      {StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser},
      {StrId::STR_OPDS_BROWSER, SettingAction::BrowseOPDS},
      {StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache},
      {StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates},
      {StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate},
      {StrId::STR_ABOUT, SettingAction::About},
  };
  // Home button settings are edited via the HomeButton action row's sub-screen
  // (below), not as individual rows in the flat Controls list (#3516).
  // longPressMenuFunction (legacy long-press-Confirm cycling) is superseded by
  // the Home button's own long-press action on home-key boards. The row count
  // below and the list build apply the same rule, so every tab reserves exactly.

  const auto& catalog = getBaseSettingsList();
  std::array<size_t, settingstabs::TAB_COUNT> rowCounts{};
  for (const auto& setting : catalog) {
    const int tab = deviceSettingsTab(setting);
    if (tab >= 0 && listedAsRow(setting)) ++rowCounts[tab];
  }
  for (const auto& row : DONG_HANH_DONG)
    if (!infoupdate::holds(row.viec)) ++rowCounts[static_cast<int>(settingstabs::nhaCua(row.viec))];
  if (!BoardConfig::hasTouch()) ++rowCounts[static_cast<int>(settingstabs::Tab::CONTROLS)];
  if (keyboard_layouts::COUNT > 1) ++rowCounts[static_cast<int>(settingstabs::Tab::KEYBOARD)];
  if (BoardConfig::hasHomeKey()) ++rowCounts[static_cast<int>(settingstabs::Tab::CONTROLS)];
  ++rowCounts[static_cast<int>(settingstabs::Tab::SYSTEM)];
  ++rowCounts[static_cast<int>(settingstabs::Tab::DEVICE)];
  rowCounts[static_cast<int>(settingstabs::Tab::READER)] += 3 + (!dictionaries.empty() ? 1 : 0);
  for (size_t tab = 0; tab < rowCounts.size(); ++tab)
    danhSachCuaThe(static_cast<settingstabs::Tab>(tab)).reserve(rowCounts[tab]);

  for (const auto& setting : catalog) {
    const int tab = deviceSettingsTab(setting);
    if (tab < 0) continue;
    if (setting.valuePtr == &CrossPointSettings::statusBarClock) {
      const auto afterLabels = std::find_if(displaySettings.begin(), displaySettings.end(), [](const SettingInfo& row) {
        return row.valuePtr == &CrossPointSettings::tenorButtonSymbols;
      });
      displaySettings.insert(afterLabels == displaySettings.end() ? afterLabels : afterLabels + 1,
                             buildTenorClockPlacementSetting(setting));
    } else if (listedAsRow(setting)) {
      danhSachCuaThe(static_cast<settingstabs::Tab>(tab)).push_back(setting);
    }
  }
  // This descriptor and its closures own the copied dictionary names across
  // child pickers and Back, after the discovery vector is destroyed.
  if (!dictionaries.empty()) readerSettings.push_back(buildDictionarySetting(dictionaries));

  if (!BoardConfig::hasTouch()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
  for (const auto& dong : DONG_HANH_DONG) {
    if (infoupdate::holds(dong.viec)) continue;  // X4 Pro: on the About & updates screen
    danhSachCuaThe(settingstabs::nhaCua(dong.viec)).push_back(SettingInfo::Action(dong.nhan, dong.viec));
  }
  // One layout leaves nothing to pick, so the row would be a dead press.
  if (keyboard_layouts::COUNT > 1) {
    keyboardSettings.insert(keyboardSettings.begin(),
                            SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts));
  }
  // Home button shortcuts (#3516): a physical Home key only.
  if (BoardConfig::hasHomeKey()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_HOME_BUTTON, SettingAction::HomeButton));
  }
  // Clock precedes file-management preferences. It stays on boards without an
  // RTC: the time comes from NTP there, and the zone and format still apply.
  const auto files = std::find_if(systemSettings.begin(), systemSettings.end(), [](const SettingInfo& row) {
    return row.valuePtr == &CrossPointSettings::showHiddenFiles;
  });
  systemSettings.insert(files, SettingInfo::Action(StrId::STR_CLOCK, SettingAction::ClockSettings));
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  // Read only, last on Device: the panel chip, for a photo sent with an ink report.
  if (!infoupdate::holds(SettingAction::None, /*chip=*/true)) {
    SettingInfo chip = SettingInfo::Action(StrId::STR_DISPLAY_CHIP, SettingAction::None);
    chip.stringGetter = [] { return panelchip::current(); };
    deviceSettings.push_back(std::move(chip));
  }

  // A theme or conditional row can shorten an inactive category as well.
  for (size_t tab = 0; tab < tabNavs.size(); ++tab) {
    const int count = static_cast<int>(danhSachCuaThe(static_cast<settingstabs::Tab>(tab)).size());
    auto& cursor = tabNavs[tab];
    cursor.selected = count == 0 ? 0 : std::clamp(cursor.selected.load(), mappedInput.hasTouch() ? 0 : 1, count);
    cursor.followOnBuild = true;
  }
  currentSettings = &danhSachCuaThe(static_cast<settingstabs::Tab>(selectedCategoryIndex));
  settingsCount = static_cast<int>(currentSettings->size());
  rebuildRowItems();
  if (shell::isUgly()) bindForm();
}

void SettingsActivity::onEnter() {
  RenderLock lock(*this);
  navigationPrefix = tr(STR_SETTINGS_TITLE);
  UiTabListActivity::onEnter();

  // Mo tai the nguoi goi dat. Con tro van dung o thanh the (vong 0), do lop nen tu dat.
  selectedCategoryIndex = theBanDau;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  if (shell::isUgly()) ugly::ensureFonts(renderer);
  rebuildSettingsLists(true);
  dapXuongNhom();
}

void SettingsActivity::restoreNavigation(const MenuNavigationState& state) {
  MenuNavigationState restored = state;
  if (fromHomeGroup) restored.tab = theBanDau;
  UiTabListActivity::restoreNavigation(restored);
  if (shell::isUgly()) {
    RenderLock lock(*this);
    focusForm(std::clamp(ringPos() - 1, 0, std::max(0, settingsCount - 1)));
  }
}

void SettingsActivity::onPause() {
  form_.invalidate();
  formPaintReady_.store(false);
  formCount_ = 0;
  if (!releaseListsForFontDownload_) return;
  // ActivityManager holds RenderLock and has already saved navigation. The
  // download child owns its data, so these descriptors and rows can be rebuilt
  // on return. Other children can retain descriptor pointers and keep them.
#ifdef ESP_PLATFORM
  const uint32_t before = ESP.getFreeHeap();
#endif
  closeRouting();
  currentSettings = nullptr;
  settingsCount = 0;
  std::vector<fui::ListItem>().swap(rowItems_);
  std::vector<std::string>().swap(rowValues_);
  for (int tab = 0; tab < settingstabs::TAB_COUNT; ++tab) {
    std::vector<SettingInfo>().swap(danhSachCuaThe(static_cast<settingstabs::Tab>(tab)));
  }
#ifdef ESP_PLATFORM
  const uint32_t after = ESP.getFreeHeap();
  LOG_INF("SETTINGS", "Font download rows released=%u heap=%u largest=%u", after >= before ? after - before : 0u,
          after, ESP.getMaxAllocHeap());
#endif
}

void SettingsActivity::onResume() {
  // ActivityManager owns RenderLock here.
  if (releaseListsForFontDownload_) {
    rebuildSettingsLists(true);
    releaseListsForFontDownload_ = false;
  } else if (shell::isUgly()) {
    bindForm();
  }
}

bool SettingsActivity::handleHomeGesture() {
  if (!shell::isUgly()) return false;
  queueForm({FormEvent::Type::Key, ugly::QuestionSheet::Key::Home});
  return true;
}

void SettingsActivity::selectCategory(const int categoryIndex) {
  // Same render-vs-button race selectTab() documents in the reader menu: the
  // render task reads currentSettings/rowItems_ mid-build while a tab step
  // replaces them from the unlocked button path.
  RenderLock lock(*this);
  selectedCategoryIndex = categoryIndex;
  currentSettings = &danhSachCuaThe(static_cast<settingstabs::Tab>(selectedCategoryIndex));
  settingsCount = static_cast<int>(currentSettings->size());
  // Pull the viewport to this tab's remembered row. UiTabListActivity owns the
  // remember/forget rule for every tab screen; see rowTab there.
  dapXuongNhom();
  rebuildRowItems();
  if (shell::isUgly()) bindForm();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for *currentSettings.
// Structural - call only when the active category or a category's setting
// list changes, never from buildScreen(), which only refreshes rowValues_
// content and rowItems_[].value pointers in place.
void SettingsActivity::rebuildRowItems() {
  const auto& settings = *currentSettings;
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(settings.size());
  for (size_t i = 0; i < settings.size(); i++) {
    fui::ListItem item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    item.opensNext = settingOpensNext(settings[i]);
    rowItems_.push_back(item);
  }
}

void SettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectCategory(index);
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void SettingsActivity::activateIndex(const int index) {
  if (shell::isUgly()) {
    if (!currentSettings || index < 0 || index >= settingsCount) return;
    ugly::QuestionSheet::Intent intent;
    {
      RenderLock lock(*this);
      focusForm(index);
      const bool paperWasOpen = form_.paperOpen();
      const auto row = formRow(this, index);
      if (row.count > 0 && (row.kind == ugly::QuestionSheet::Kind::Toggle ||
                           (row.kind == ugly::QuestionSheet::Kind::Choice && row.count <= 6))) {
        intent = {ugly::QuestionSheet::IntentKind::Commit, row.id, index, (row.selected + 1) % row.count, true};
      } else {
        intent = form_.input(ugly::QuestionSheet::Key::Confirm);
      }
      if (paperWasOpen != form_.paperOpen()) ++formSurface_;
      formPaintReady_.store(false);
    }
    applyFormIntent(intent);
    return;
  }
  if (optionPopup.isActive()) return;
  // toggleCurrentSetting reads the ring position; a tap on the touch shell leaves the ring alone
  // (no cursor row), so it names the row it landed on here.
  if (tenorchrome::kTouchShell) activeNav().selected = index + 1;
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  toggleCurrentSetting();
  // Tap-first: a tapped row is not a cursor position. Leaving it focused
  // (inverted) after the tap meant the row stayed black once its sub-screen or
  // popup closed, and Back then had to clear that focus before a second Back
  // left Settings. Hand the focus back to the tab band; the viewport stays put.
  if (mappedInput.hasTouch()) {
    activeNav().selected = 0;
  }
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

bool SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr, const uint8_t newValue) {
  if (valuePtr == &CrossPointSettings::uiTextSize) {
    RenderLock lock(*this);
    if (!applyUiTextSize(renderer, newValue)) {
      LOG_ERR("SETTINGS", "Applying UI text size failed");
      return false;
    }
    resetUi();
    return true;
  }
  return true;
}

uint8_t SettingsActivity::formValue(const SettingInfo& setting, const int option) {
  return static_cast<uint8_t>(setting.type == SettingType::VALUE
                                  ? setting.valueRange.min + option * setting.valueRange.step
                                  : option);
}

ugly::QuestionSheet::Row SettingsActivity::formRow(void* context, const int index) {
  using Kind = ugly::QuestionSheet::Kind;
  auto& self = *static_cast<SettingsActivity*>(context);
  if (!self.currentSettings || index < 0 || index >= self.settingsCount) return {};
  const auto& setting = (*self.currentSettings)[index];
  ugly::QuestionSheet::Row row;
  row.id = static_cast<uint32_t>(setting.nameId) + 1;
  row.question = I18N.get(setting.nameId);
  const int value = setting.valuePtr ? SETTINGS.*setting.valuePtr : setting.valueGetter ? setting.valueGetter() : 0;
  if (setting.type == SettingType::TOGGLE && (setting.valuePtr || setting.valueSetter)) {
    row.kind = Kind::Toggle;
    row.count = 2;
    row.selected = value;
  } else if (setting.type == SettingType::ENUM && (setting.valuePtr || setting.valueSetter)) {
    row.count = static_cast<int>(setting.enumStringValues.empty() ? setting.enumLabels().size()
                                                                 : setting.enumStringValues.size());
    row.kind = row.count >= 7 ? Kind::Paper : Kind::Choice;
    row.selected = value;
  } else if (setting.type == SettingType::VALUE && setting.valueRange.step) {
    row.count = (setting.valueRange.max - setting.valueRange.min) / setting.valueRange.step + 1;
    row.kind = row.count <= 6 ? Kind::Choice : row.count <= 11 ? Kind::Ruler : Kind::Paper;
    row.selected = (value - setting.valueRange.min) / setting.valueRange.step;
  } else {
    row.kind = setting.type == SettingType::ACTION && setting.action != SettingAction::None ? Kind::Action : Kind::ReadOnly;
  }
  return row;
}

void SettingsActivity::formLabel(void* context, const int row, const int option, char* out, const size_t size) {
  if (!size) return;
  out[0] = 0;
  auto& self = *static_cast<SettingsActivity*>(context);
  if (!self.currentSettings || row < 0 || row >= self.settingsCount) return;
  const auto& setting = (*self.currentSettings)[row];
  const char* label = "";
  if (setting.type == SettingType::TOGGLE) {
    label = setting.valuePtr == &CrossPointSettings::keyboardAxisSwapped
                ? (option ? tr(STR_KEYBOARD_MOVE_VERTICAL) : tr(STR_KEYBOARD_MOVE_HORIZONTAL))
                : (option ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  } else if (setting.type == SettingType::ENUM) {
    if (!setting.enumStringValues.empty() && option >= 0 && option < static_cast<int>(setting.enumStringValues.size()))
      label = setting.enumStringValues[option].c_str();
    else if (option >= 0 && option < static_cast<int>(setting.enumLabels().size())) label = I18N.get(setting.enumLabels()[option]);
  } else if (setting.type == SettingType::VALUE) {
    const unsigned value = formValue(setting, option);
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (value >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) label = tr(STR_SLEEP_NEVER);
      else { snprintf(out, size, tr(STR_SLEEP_TIMER_VALUE_FORMAT), value); return; }
    } else { snprintf(out, size, "%u", value); return; }
  } else if (setting.stringGetter) {
    // Read-only catalog services return their current label; no SD discovery.
    const std::string value = setting.stringGetter();
    snprintf(out, size, "%s", value.c_str());
    return;
  }
  snprintf(out, size, "%s", label);
}

void SettingsActivity::focusForm(const int row) {
  if (row < 0 || row >= settingsCount) return;
  const int previousSheet = form_.sheet();
  for (int n = 0; form_.question() != row && n < settingsCount; ++n)
    form_.input(ugly::QuestionSheet::Key::NextQuestion);
  if (previousSheet != form_.sheet()) ++formSurface_;
  activeNav().selected = row + 1;
}

void SettingsActivity::bindForm() {
  ++formSurface_;
  ugly::QuestionSheet::View view;
  view.context = this;
  view.count = settingsCount;
  view.subject = tabLabel(selectedCategoryIndex);
  view.date = ReadingStatsStore::currentDay();
  view.code = selectedCategoryIndex + 1;
  view.row = formRow;
  view.label = formLabel;
  form_.bind(renderer, view, mappedInput.hasTouch());
  focusForm(std::clamp(ringPos() - 1, 0, std::max(0, settingsCount - 1)));
  formPaintReady_.store(false);
}

void SettingsActivity::prepareFormQuip(const int row, const int candidate) {
  if (!currentSettings || row < 0 || row >= settingsCount) return;
  const auto& setting = (*currentSettings)[row];
  const int value = formValue(setting, std::max(0, candidate));
  auto line = ugly::quip(ugly::Quip::SetValue, ugly::valueKey(setting, value), 0, value);
  RenderLock lock(*this);
  form_.setQuip(row, std::move(line));
}

void SettingsActivity::applyFormIntent(const ugly::QuestionSheet::Intent& intent) {
  using Intent = ugly::QuestionSheet::IntentKind;
  if (intent.kind == Intent::Back) {
    formCount_ = 0;
    if (!saveSettings(false)) { requestUpdate(); return; }
    if (fromHomeGroup) finish();
    else onGoHome();
    return;
  }
  if (intent.row >= 0 && currentSettings && intent.row < settingsCount &&
      intent.id == formRow(this, intent.row).id) {
    if (intent.kind == Intent::Commit) {
      const int previous = formRow(this, intent.row).selected;
      if (applySettingValue(intent.row, formValue((*currentSettings)[intent.row], intent.candidate))) {
        RenderLock lock(*this);
        // RAM is authoritative for the pencil mark; saveFailed separately
        // reports persistence failure and Back stays here until retry succeeds.
        form_.didCommit(intent.row, previous);
      }
    } else if (intent.kind == Intent::Activate) {
      formCount_ = 0;
      toggleCurrentSetting();
      // A failed service allocation leaves this same form available.
      formPaintReady_.store(true);
      return;
    }
    if (intent.repaint) prepareFormQuip(intent.row, intent.candidate);
  }
  if (intent.repaint) requestUpdate();
}

void SettingsActivity::queueForm(FormEvent event) {
  if (event.type == FormEvent::Type::Tap || event.type == FormEvent::Type::Hold || event.type == FormEvent::Type::Strike)
    event.surface = formVisibleSurface_.load();
  if (formCount_ < formQueue_.size()) formQueue_[(formHead_ + formCount_++) % formQueue_.size()] = event;
  else LOG_ERR("UGLY", "Settings form queue full");
}

void SettingsActivity::pollTilt() {
  if (!shell::isUgly()) UiTabListActivity::pollTilt();
}

bool SettingsActivity::handleCustomInput() {
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
    const bool paperWasOpen = form_.paperOpen();
    const int previousSheet = form_.sheet();
    const int previousPaperFirst = form_.paperFirst();
    if (event.type == FormEvent::Type::Key) intent = form_.input(event.key);
    else if (event.type == FormEvent::Type::Tap) intent = form_.tap(event.x, event.y);
    else if (event.type == FormEvent::Type::Strike) strikeRow = form_.paperOpen() ? -1 : form_.questionAt(event.x, event.y);
    else if (!form_.paperOpen()) pinRow = event.type == FormEvent::Type::Hold ? form_.questionAt(event.x, event.y) : form_.question();
    if (paperWasOpen != form_.paperOpen() || previousSheet != form_.sheet() ||
        (form_.paperOpen() && previousPaperFirst != form_.paperFirst())) ++formSurface_;
    activeNav().selected = form_.question() + 1;
    if (intent.repaint || pinRow >= 0 || strikeRow >= 0) formPaintReady_.store(false);
  }
  if (strikeRow >= 0) {
    const auto valuePtr = (*currentSettings)[strikeRow].valuePtr;
    const int previous = formRow(this, strikeRow).selected;
    if (valuePtr && applySettingValue(strikeRow, CrossPointSettings::defaultOf(valuePtr))) {
      RenderLock lock(*this);
      form_.didCommit(strikeRow, previous);
    }
    requestUpdate();
  } else if (pinRow >= 0 && !favoriteKey(pinRow).empty()) {
    const bool wasPinned = rowIsPinned(pinRow);
    formPinFailed_.store(!toggleFavorite(pinRow));
    auto line = ugly::quip(wasPinned ? ugly::Quip::Unpin : ugly::Quip::Pin);
    { RenderLock lock(*this); form_.setQuip(pinRow, std::move(line)); }
    requestUpdate();
  } else {
    // An inert hold must leave the input-to-paint handshake open.
    if (pinRow >= 0) formPaintReady_.store(true);
    applyFormIntent(intent);
  }
  return true;
}

void SettingsActivity::stepTab(const int direction) {
  // The new category keeps whatever row the cursor was last on there; the two
  // tab buttons sit on the device edge, so stepping away by accident and back
  // must not lose the reader's place.
  selectedCategoryIndex = adjacentTab(direction);
  selectCategory(selectedCategoryIndex);
  dapXuongNhom();
  requestUpdate();
}

void SettingsActivity::dapXuongNhom() {
  auto& n = activeNav();
  n.selected = settingsCount <= 0 ? 0 : std::clamp(n.selected.load(), mappedInput.hasTouch() ? 0 : 1, settingsCount);
  n.followOnBuild = true;
}

void SettingsActivity::nhanNhom() {
  // moveRingTo la cho duy nhat doi moc rowTab va xoa cho nho cua nhom khac.
  if (ringPos() > 0) commitTabNavigation();
}

void SettingsActivity::navigateButtons() {
  if (handleTabHoldNavigation()) return;
  // Vong N dong, quay vong, khong co vi tri thanh the. Cap nut mat truoc di dong; nhip di la
  // "bam di trong nhom", tuc nhan nhom (luat nho). Hai nut canh nhay nhom nhu moi man the.
  const int n = settingsCount;
  const auto toi = [this, n](const int dong) {
    if (n <= 0) return;
    moveRingTo(dong);
  };
  const auto next = [this, n, toi] { toi(ringPos() >= n ? 1 : ringPos() + 1); };
  const auto previous = [this, n, toi] { toi(ringPos() <= 1 ? n : ringPos() - 1); };
  buttonNavigator.onRelease({MappedInputManager::Button::Right}, next);
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, previous);

  tabNavigator.onRelease({MappedInputManager::Button::Down}, [this] { queueNavIntent(NavIntent::TabNext); });
  tabNavigator.onRelease({MappedInputManager::Button::Up}, [this] { queueNavIntent(NavIntent::TabPrev); });
}

bool SettingsActivity::saveSettings(const bool repaint) {
  const bool saved = SETTINGS.saveToFile();
  saveFailed.store(!saved);
  if (!saved) LOG_ERR("SETTINGS", "Saving settings failed");
  if (repaint) requestUpdate();
  return saved;
}

bool SettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    nhanNhom();
    toggleCurrentSetting();
    requestUpdate();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Keep the error visible so the user can restore storage and retry Back.
    if (!saveSettings()) return true;
    if (fromHomeGroup) {
      finish();
    } else {
      onGoHome();
    }
    return true;
  }

  return false;
}

bool SettingsActivity::applySettingValue(const int row, const uint8_t value, const bool shellConfirmed) {
  if (!currentSettings || row < 0 || row >= settingsCount) return false;
  const auto& setting = (*currentSettings)[row];
  const auto valuePtr = setting.valuePtr;
  if (!valuePtr && !(setting.valueGetter && setting.valueSetter)) return false;
  if (setting.type == SettingType::TOGGLE && value > 1) return false;
  if (setting.type == SettingType::ENUM) {
    const size_t count = setting.enumStringValues.empty() ? setting.enumLabels().size() : setting.enumStringValues.size();
    if (value >= count) return false;
  } else if (setting.type == SettingType::VALUE) {
    const auto range = setting.valueRange;
    if (!range.step || value < range.min || value > range.max || (value - range.min) % range.step) return false;
  } else if (setting.type != SettingType::TOGGLE) {
    return false;
  }
  const uint8_t current = valuePtr ? SETTINGS.*valuePtr : setting.valueGetter();
  if (current == value) return true;
  if (valuePtr == &CrossPointSettings::uiShell && !shellConfirmed) {
    const bool toCross = value == static_cast<uint8_t>(shell::Kind::Cross);
    startActivityForResult(ugly::makeSwitchConfirm(renderer, mappedInput, toCross), [this, row, value](const ActivityResult& result) {
      if (!result.isCancelled) applySettingValue(row, value, true);
      else {
        if (shell::isUgly()) prepareFormQuip(row, formRow(this, row).selected);
        requestUpdate();
      }
    });
    return false;
  }
  if (valuePtr == &CrossPointSettings::uiTextSize) {
    if (!applyUiSettingChange(valuePtr, value)) {
      if (!shell::isUgly()) requestUpdate();
      return false;
    }
  } else {
    RenderLock lock(*this);
    if (valuePtr) SETTINGS.*valuePtr = value;
    else setting.valueSetter(value);
    syncQuickResumeTimeoutForSleepScreen(valuePtr == &CrossPointSettings::sleepScreen,
                                        valuePtr == &CrossPointSettings::quickResumeSleepScreen);
  }
  if (valuePtr != &CrossPointSettings::uiTextSize && !applyUiSettingChange(valuePtr, current)) {
    if (!shell::isUgly()) requestUpdate();
    return false;
  }
  if (valuePtr != &CrossPointSettings::uiShell) saveSettings(!shell::isUgly());
  noteValue(setting.nameId);
  // Ordinary value commits keep the form's previous pencil marks. Only a
  // structural shell change rebuilds the catalog; UI size changes geometry.
  if (!shell::isUgly() || valuePtr == &CrossPointSettings::uiShell) rebuildSettingsLists();
  else if (valuePtr == &CrossPointSettings::uiTextSize || valuePtr == &CrossPointSettings::uiUglyLevel) {
    RenderLock lock(*this);
    bindForm();
  }
  if (valuePtr == &CrossPointSettings::uiShell) shell::changed();
  return true;
}

void SettingsActivity::toggleCurrentSetting() {
  mappedInput.resetHomeButtonInput();
  const int row = ringPos() - 1;
  if (!currentSettings || row < 0 || row >= settingsCount) return;
  const auto& setting = (*currentSettings)[row];
  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }
  if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { saveSettings(); };

    switch (setting.action) {
      case SettingAction::HomeButton: {
        // The row only exists with a home key; a build without one leaves this screen out.
        if (!BoardConfig::hasHomeKey()) return;
        // Activities must outlive this call and are owned by the activity stack.
        auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SET", "OOM: Home button settings");
          return;
        }
        startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
        return;
      }
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), [this](const ActivityResult&) { requestUpdate(); });
        break;
      case SettingAction::ClockSettings:
        if (auto activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SETTINGS", "OOM: ClockSettingsActivity");
        }
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::FileTransfer:
        activityManager.goToFileTransfer();
        break;
      case SettingAction::BrowseOPDS:
        activityManager.goToBrowser();
        break;
      case SettingAction::BlePageTurner:
        startActivityForResult(std::make_unique<BlePageTurnerActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DeviceName:
        startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                                   renderer, mappedInput, tr(STR_DEVICE_NAME), std::string(SETTINGS.deviceName),
                                   sizeof(SETTINGS.deviceName) - 1, InputType::Text),
                               [this](const ActivityResult& result) {
                                 if (result.isCancelled) return;
                                 const auto& kb = std::get<KeyboardResult>(result.data);
                                 strncpy(SETTINGS.deviceName, kb.text.c_str(), sizeof(SETTINGS.deviceName) - 1);
                                 SETTINGS.deviceName[sizeof(SETTINGS.deviceName) - 1] = '\0';
                                 saveSettings();
                                 requestUpdate();
                               });
        break;
      case SettingAction::Network: {
        auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: WifiSelectionActivity");
          return;
        }
        startActivityForResult(std::move(activity), [](const ActivityResult&) {
          SETTINGS.saveToFile();
          // Every other WiFi consumer hands the radio to a session it owns;
          // these rows only save credentials, so nothing here would ever
          // release the driver's heap. The scan alone brings it up, so tear
          // down whether or not the user joined a network.
          if (WiFi.getMode() == WIFI_MODE_NULL) return;
          WiFi.disconnect(false);
          delay(30);
          // Unlike the onExit() teardowns, this runs from the loop task with
          // no lock held; the restart popup paints straight to the panel.
          RenderLock lock;
          silentRestartToSettings();
        });
        break;
      }
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        releaseListsForFontDownload_ = true;
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 saveSettings();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 saveSettings();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::KeyboardLayouts:
        if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: KeyboardLayoutsActivity");
        }
        break;
      case SettingAction::About:
        if (auto activity = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: AboutActivity");
        }
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;
  }
  const uint8_t current = setting.valuePtr ? SETTINGS.*setting.valuePtr : setting.valueGetter ? setting.valueGetter() : 0;
  if (setting.type == SettingType::TOGGLE) {
    applySettingValue(row, !current);
  } else if (setting.type == SettingType::ENUM) {
    const int count = static_cast<int>(setting.enumStringValues.empty() ? setting.enumLabels().size() : setting.enumStringValues.size());
    if (count <= 0) return;
    if (settingstabs::moTrinhChon(count)) {
      auto onSelect = [this, row](const int index) { applySettingValue(row, static_cast<uint8_t>(index)); };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, current, std::move(onSelect));
      } else {
        const auto labels = setting.enumLabels();
        optionPopup.show(setting.nameId, labels.data(), count, current, std::move(onSelect));
      }
      requestUpdate();
    } else {
      applySettingValue(row, static_cast<uint8_t>((current + 1) % count));
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr) {
    const auto range = setting.valueRange;
    applySettingValue(row, current + range.step > range.max ? range.min : current + range.step);
  }
}

void SettingsActivity::noteValue(const StrId name) {
  for (const auto& row : *currentSettings)
    if (row.nameId == name) return shell::valueChanged(row);
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          for (int row = 0; row < settingsCount; ++row)
            if ((*currentSettings)[row].nameId == StrId::STR_TIME_TO_SLEEP) {
              applySettingValue(row, static_cast<uint8_t>(std::get<IntervalResult>(result.data).value));
              break;
            }
        }
        requestUpdate();
      });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
  if (setting.action == SettingAction::HomeButton) return tr(STR_CONFIGURE);
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    if (setting.valuePtr == &CrossPointSettings::keyboardAxisSwapped) {
      return SETTINGS.keyboardAxisSwapped ? tr(STR_KEYBOARD_MOVE_VERTICAL) : tr(STR_KEYBOARD_MOVE_HORIZONTAL);
    }
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (value >= enumLabels.size()) return "";
    return I18N.get(enumLabels[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    const auto enumLabels = setting.enumLabels();
    if (value < enumLabels.size()) {
      return I18N.get(enumLabels[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::ACTION && setting.stringGetter) return setting.stringGetter();
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
      static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  // Cac nhom da hien mot lan o man chinh, hien lai lan nua
  // la trung (T1). Ten nhom di len dau man, hai mui tien dac hai mep bao nut canh nhay nhom.
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the
  // category was last selected/rebuilt; only the live value text needs
  // refreshing here, by assigning into the existing rowValues_ strings (no
  // vector growth) rather than building a new items/values vector on every
  // render.
  const auto& settings = *currentSettings;
  for (size_t i = 0; i < settings.size(); i++) {
    rowValues_[i] = settingValueText(settings[i]);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  if (tenorchrome::kTouchShell) props.inputMask |= fui::InputLongPress;  // hold a row: its menu (Pin)
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  // A row's note (the limited edition under Interface) wraps too, so no settings row cuts its words.
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

// Ten nhom o dau man, kep giua hai mui ten DAC, be (cung co mui ten cua thanh the chay). Cac
// nhom quay vong nen luon con nhom ca hai ben, ve ca hai. Ve tay o day vi drawHeader chi nhan mot
// chuoi tieu de; hai mui ten dat ngay canh chu de mat doc "< Hien thi >" thanh mot cum.
void SettingsActivity::veTenNhomCoMuiTen(const GfxRenderer& r, const int x0, const int yGiua, const char* ten) {
  constexpr int RONG = 7, CAO = 12, HO = 7;
  const int fontId = uiScaleSpec().titleFontId;
  const int chuCao = r.getTextHeight(fontId);
  const int chuRong = r.getTextWidth(fontId, ten, EpdFontFamily::BOLD);
  // drawText nhan y la MEP TREN cua o chu, va chuCao gom ca phan duoi dong, nen tam mat cua
  // chu hoa nam thap hon yGiua mot chut. Do tren simulator 14/09: chuCao/4 la vua.
  const int yChu = yGiua - chuCao / 2;
  const int yMui = yGiua + chuCao / 4;
  const auto muiTen = [&](const int xMui, const int chieu) {
    for (int i = 0; i < RONG; i++) {
      const int nua = (CAO / 2) * i / (RONG - 1);
      const int x = xMui + chieu * i;
      r.drawLine(x, yMui - nua, x, yMui + nua, true);
    }
  };
  muiTen(x0, 1);  // mui trai, mui o x0, than mo sang phai
  const int xChu = x0 + RONG + HO;
  r.drawText(fontId, xChu, yChu, ten, true, EpdFontFamily::BOLD);
  muiTen(xChu + chuRong + HO + RONG - 1, -1);  // mui phai, mui o cuoi, than mo sang trai
}

bool SettingsActivity::selectSettingsSibling(const int direction) {
  if (!currentSettings || settingsCount < 2) return false;
  const auto isConfiguration = [](const SettingInfo& item) {
    if (item.type != SettingType::ACTION) return false;
    switch (item.action) {
      case SettingAction::RemapFrontButtons:
      case SettingAction::CustomiseStatusBar:
      case SettingAction::KOReaderSync:
      case SettingAction::OPDSBrowser:
      case SettingAction::TextSettings:
      case SettingAction::Language:
      case SettingAction::KeyboardLayouts:
      case SettingAction::ClockSettings:
        return true;
      default:
        return false;
    }
  };
  const int from = ringPos() - 1;
  if (from < 0 || from >= settingsCount || !isConfiguration((*currentSettings)[from])) return false;
  for (int distance = 1; distance < settingsCount; ++distance) {
    const int index = (from + direction * distance + settingsCount) % settingsCount;
    if (isConfiguration((*currentSettings)[index])) {
      pendingSiblingIndex = index;
      return true;
    }
  }
  return false;
}

bool SettingsActivity::openPendingSettingsSibling() {
  if (pendingSiblingIndex < 0) return false;
  const int index = pendingSiblingIndex;
  pendingSiblingIndex = -1;
  {
    RenderLock lock(*this);
    activeNav().selected = index + 1;
  }
  activateIndex(index);
  return true;
}

void SettingsActivity::render(RenderLock&&) {
  if (shell::isUgly()) {
    [[maybe_unused]] const uint32_t started = millis();
    form_.paint(renderer, mappedInput);
    if (saveFailed.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));
    else if (formPinFailed_.load()) GUI.drawPopup(renderer, tr(STR_MENU_SAVE_FAILED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    formVisibleSurface_.store(formSurface_);
    formPaintReady_.store(true);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "Settings form total=%lums tab=%d rows=%d sheet=%d/%d question=%d candidate=%d paper=%d",
            static_cast<unsigned long>(millis() - started), selectedCategoryIndex, settingsCount, form_.sheet() + 1,
            form_.sheetCount(), form_.question(), form_.candidate(), form_.paperOpen());
#endif
    return;
  }
  if (optionPopup.processRender(renderer, mappedInput)) return;
  if (rowMenu.processRender(renderer, mappedInput)) return;

  // Tenor tab chrome: settled-list debounce, nav header, sibling-tab arrows.
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawNavigationHeader(tabLabel(activeTab()));
    renderUi();
  });

  // Touch: no sibling line, the only way back is "<" in the bar at the foot.
  if (tenorchrome::enabled() && !tenorchrome::kTouchShell && tabCount() > 1) {
    tenorchrome::drawSiblingDestinations(renderer, tabLabel(adjacentTab(-1)), tabLabel(adjacentTab(1)));
  }

  const int ring = ringPos();
  // The two edge buttons already move between tabs, so Confirm on the tab band
  // steps into the tab's rows instead of stepping the tab. Labelling it with the
  // next tab's name read like a command rather than a destination.
  const auto confirmLabel =
      (ring <= 0 || ring > settingsCount)
          ? tr(STR_SELECT)
          : ((*currentSettings)[ring - 1].nameId == StrId::STR_TIME_TO_SLEEP ? tr(STR_SELECT) : tr(STR_TOGGLE));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (saveFailed.load()) GUI.drawPopup(renderer, tr(STR_HABIT_SAVE_FAILED));

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}

std::string SettingsActivity::favoriteKey(const int row) const {
  if (!currentSettings || row < 0 || row >= settingsCount) return {};
  const auto& item = (*currentSettings)[row];
  if (item.key) return std::string("settings/") + item.key;
  // Tenor keeps the old action/2 pin pointing at the battery and clock corners
  // (focusFavorite below), so this row cannot be pinned there under that key.
  if (item.action == SettingAction::CustomiseStatusBar)
    return {};
  if (item.action != SettingAction::None) return "action/" + std::to_string(static_cast<int>(item.action));
  return {};
}
int SettingsActivity::focusFavorite(const std::string& key) {
  // Existing action pins keep their key after the Tenor-only child is removed.
  const std::string target = key == "action/2" ? "settings/statusBarClock" : key;
  for (int tab = 0; tab < categoryCount; ++tab) {
    const auto& items = danhSachCuaThe(static_cast<settingstabs::Tab>(tab));
    for (size_t row = 0; row < items.size(); ++row) {
      const auto& item = items[row];
      const std::string candidate =
          item.key ? std::string("settings/") + item.key : "action/" + std::to_string(static_cast<int>(item.action));
      if (candidate != target) continue;
      selectCategory(tab);
      {
        RenderLock lock(*this);
        activeNav().selected = row + 1;
        activeNav().followOnBuild = true;
        if (shell::isUgly()) focusForm(static_cast<int>(row));
      }
      return static_cast<int>(row);
    }
  }
  return -1;
}
