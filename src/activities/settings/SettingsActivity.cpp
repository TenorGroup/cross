#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BlePageTurnerActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "DongHoSettingsActivity.h"
#include "FontDownloadActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int theBanDau,
                                   const bool fromHomeGroup)
    : UiTabListActivity("Settings", renderer, mappedInput),
      fromHomeGroup(fromHomeGroup),
      theBanDau(theBanDau >= 0 && theBanDau < settingstabs::TAB_COUNT ? theBanDau : 0) {}

std::vector<SettingInfo>& SettingsActivity::danhSachCuaThe(const settingstabs::Tab tab) {
  switch (tab) {
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
  }
  return systemSettings;
}

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  deviceSettings.clear();
  otherSettings.clear();
  keyboardSettings.clear();
  keyboardSettings.reserve(4);

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran - otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  for (const auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries)) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI &&
        setting.valuePtr == &CrossPointSettings::hideBatteryPercentage)
      continue;
    if (SETTINGS.uiTheme != CrossPointSettings::TENOR_UI &&
        (setting.valuePtr == &CrossPointSettings::tenorButtonSymbols ||
         setting.valuePtr == &CrossPointSettings::tenorSideArrows))
      continue;
    if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && halClock.isAvailable() &&
        setting.valuePtr == &CrossPointSettings::statusBarClock) {
      const auto afterLabels = std::find_if(displaySettings.begin(), displaySettings.end(), [](const SettingInfo& row) {
        return row.valuePtr == &CrossPointSettings::tenorButtonSymbols;
      });
      displaySettings.insert(afterLabels == displaySettings.end() ? afterLabels : afterLabels + 1,
                             buildTenorClockPlacementSetting(setting));
    } else if (setting.category == StrId::STR_CAT_DISPLAY) {
      // The sunlight fading fix is a grayscale-waveform compensation that does
      // not apply on the X4 Pro / X4 Classic (plain OTP waveform, same panels).
      if (setting.valuePtr == &CrossPointSettings::fadingFix &&
          (BoardConfig::isX4Pro() || BoardConfig::isX4Classic())) {
        continue;
      }
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_KEYBOARD) {
      keyboardSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      if (setting.valuePtr == &CrossPointSettings::wakeButtons && !gpio.deviceIsX3()) continue;
      systemSettings.push_back(setting);
    }
  }

  // Append device-only ACTION items
  if (!BoardConfig::hasTouch()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
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
  };
  for (const auto& dong : DONG_HANH_DONG) {
    danhSachCuaThe(settingstabs::nhaCua(dong.viec)).push_back(SettingInfo::Action(dong.nhan, dong.viec));
  }
  keyboardSettings.insert(keyboardSettings.begin(),
                          SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts));
  // Sleep, wake and clock precede file-management preferences.
  if (halClock.isAvailable()) {
    const auto files = std::find_if(systemSettings.begin(), systemSettings.end(), [](const SettingInfo& row) {
      return row.valuePtr == &CrossPointSettings::showHiddenFiles;
    });
    systemSettings.insert(files, SettingInfo::Action(StrId::STR_CLOCK, SettingAction::Clock));
  }
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  if (SETTINGS.uiTheme != CrossPointSettings::TENOR_UI) {
    readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  }

  // A theme or conditional row can shorten an inactive category as well.
  for (size_t tab = 0; tab < tabNavs.size(); ++tab) {
    const int count = static_cast<int>(danhSachCuaThe(static_cast<settingstabs::Tab>(tab)).size());
    auto& cursor = tabNavs[tab];
    cursor.selected = count == 0 ? 0 : std::clamp(cursor.selected, mappedInput.hasTouch() ? 0 : 1, count);
    cursor.followOnBuild = true;
  }
  currentSettings = &danhSachCuaThe(static_cast<settingstabs::Tab>(selectedCategoryIndex));
  settingsCount = static_cast<int>(currentSettings->size());
  rebuildRowItems();
}

void SettingsActivity::onEnter() {
  navigationPrefix = tr(STR_SETTINGS_TITLE);
  UiTabListActivity::onEnter();

  // Mo tai the nguoi goi dat. Con tro van dung o thanh the (vong 0), do lop nen tu dat.
  selectedCategoryIndex = theBanDau;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();
  dapXuongNhom();
}

void SettingsActivity::restoreNavigation(const MenuNavigationState& state) {
  MenuNavigationState restored = state;
  if (fromHomeGroup) restored.tab = theBanDau;
  UiTabListActivity::restoreNavigation(restored);
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
  if (optionPopup.isActive()) return;
  (void)index;  // toggleCurrentSetting reads the ring position
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

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen - reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
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
  n.selected = settingsCount <= 0 ? 0 : std::clamp(n.selected, mappedInput.hasTouch() ? 0 : 1, settingsCount);
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

bool SettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    nhanNhom();
    toggleCurrentSetting();
    requestUpdate();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Roi man mot nhip, o moi vi tri.
    SETTINGS.saveToFile();
    if (fromHomeGroup) {
      finish();
    } else {
      onGoHome();
    }
    return true;
  }

  return false;
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = ringPos() - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const auto changedValuePtr = setting.valuePtr;
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (settingstabs::moTrinhChon(static_cast<int>(setting.enumValues.size()))) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                       currentValue, [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettingsLists();
                         applyUiSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (settingstabs::moTrinhChon(totalValues)) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
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
                                 SETTINGS.saveToFile();
                                 requestUpdate();
                               });
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
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
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
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
                                 SETTINGS.saveToFile();
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
      case SettingAction::Clock:
        startActivityForResult(std::make_unique<DongHoSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  rebuildSettingsLists();
  applyUiSettingChange(changedValuePtr);
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
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
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
    if (value >= setting.enumValues.size()) return "";
    return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) {
      return I18N.get(setting.enumValues[value]);
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
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
      static_cast<int16_t>(
          metrics.buttonHintsHeight +
          (selectedCategoryIndex == static_cast<int>(settingstabs::Tab::SYSTEM) && gpio.deviceIsX3() &&
                   !SETTINGS.globalStatusBarHidden()
               ? 26
               : 0)),
      0});

  // Khong con thanh 7 the o day (S1): bo 7 nhom da hien mot lan o man chinh, hien lai lan nua
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
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

// Ten nhom o dau man, kep giua hai mui ten DAC, be (cung co mui ten cua thanh the chay). Vong 7
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
      case SettingAction::Clock:
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
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  drawNavigationHeader(tabLabel(activeTab()));

  renderUi();

  if (tenorchrome::enabled() && tabCount() > 1) {
    tenorchrome::drawSiblingDestinations(renderer, tabLabel(adjacentTab(-1)), tabLabel(adjacentTab(1)));
  }

  if (selectedCategoryIndex == static_cast<int>(settingstabs::Tab::SYSTEM) && gpio.deviceIsX3()) {
    tenorchrome::drawTip(renderer, tr(STR_WAKE_POWER_HINT), 1);
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

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}

std::string SettingsActivity::favoriteKey(const int row) const {
  if (!currentSettings || row < 0 || row >= settingsCount) return {};
  const auto& item = (*currentSettings)[row];
  if (item.key) return std::string("settings/") + item.key;
  if (item.action != SettingAction::None) return "action/" + std::to_string(static_cast<int>(item.action));
  return {};
}
int SettingsActivity::focusFavorite(const std::string& key) {
  // Existing action pins keep their key after the Tenor-only child is removed.
  const std::string target = SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && key == "action/2"
                                 ? "settings/statusBarClock"
                                 : key;
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
      }
      return static_cast<int>(row);
    }
  }
  return -1;
}
