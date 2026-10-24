#include "SettingsList.h"

#include <array>
#include <utility>

// Every base descriptor, built one at a time and handed to the visitor as an lvalue it may move
// from. Settings save and load walk it here instead of keeping the whole catalog (~15 KB on the X3)
// for the life of the program. One body, out of line: a template copied the 100 rows' code into
// each caller (28 KB of flash on x3-ble).
//
// Board conditions that depend on a runtime probe do not belong here: load and save would see
// different keys. Those rows are always emitted and settingHiddenOnThisBoard() filters them.
void forEachBaseSetting(SettingVisitor visit, void* context) {
  const auto emit = [visit, context](auto&& info) { visit(context, info); };
  // Enum settings are persisted as numeric values. Assign these labels by enum
  // value so a reordered menu or enum cannot silently swap their behavior.
  std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
  sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
  sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
  sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
  sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
  sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
  sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
  sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
  sleepScreenValues[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;
  sleepScreenValues[CrossPointSettings::TENOR] = StrId::STR_SLEEP_TENOR;
  sleepScreenValues[CrossPointSettings::STATS] = StrId::STR_SLEEP_STATS;
  sleepScreenValues[CrossPointSettings::QUOTE] = StrId::STR_SLEEP_QUOTE;
  sleepScreenValues[CrossPointSettings::UGLY] = StrId::STR_SLEEP_UGLY;
  sleepScreenValues[CrossPointSettings::COVER_TRANSPARENT] = StrId::STR_COVER_TRANSPARENT;

  std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT);
  statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
  statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
  statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

  const bool hasTilt = halTiltSensor.isAvailable();
  // The motion keys are saved by the board, not by this boot's probe: an X3 whose sensor did not
  // answer once must not drop them at the next save (CrossPoint #3385). The lists hide the rows.
  const bool savesMotion = hasTilt || BoardConfig::hasImu();
  // --- Display ---
  emit(SettingInfo::Enum(StrId::STR_UI_TEXT_SIZE, &CrossPointSettings::uiTextSize,
                        {StrId::STR_UI_SIZE_SMALL, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_UI_SIZE_LARGE},
                        "uiTextSize", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_HIDE_GLOBAL_STATUS_BAR, &CrossPointSettings::globalStatusBarMode,
                        {StrId::STR_STATUS_BAR_SMALL, StrId::STR_STATE_OFF, StrId::STR_STATUS_BAR_LARGE},
                        "globalStatusBarMode", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_BUTTON_HINTS, &CrossPointSettings::tenorButtonHints, "tenorButtonHints",
                          StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_TIPS, &CrossPointSettings::tipsEnabled, "tipsEnabled",
                          StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_SIDE_ARROW_HINTS, &CrossPointSettings::tenorSideArrows, "tenorSideArrows",
                          StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_BUTTON_LABELS, &CrossPointSettings::tenorButtonSymbols,
                        {StrId::STR_LABELS_TEXT, StrId::STR_LABELS_SYMBOLS}, "tenorButtonSymbols",
                        StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                        StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                        {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                         StrId::STR_PAGES_30, StrId::STR_NEVER},
                        "refreshFrequency", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                          StrId::STR_CAT_DISPLAY));
  // Just above night mode, so the first rows and the last row of the screen group keep their place. The shell is chosen at run time: 0 tenor/cross (what every earlier release shows), 1 tenor/ugly.
  emit(SettingInfo::Enum(StrId::STR_UI_SHELL, &CrossPointSettings::uiShell,
                        {StrId::STR_SHELL_CROSS, StrId::STR_SHELL_UGLY}, "uiShell", StrId::STR_CAT_DISPLAY));
  // Right under the shell. The screen lists it only while the shell is tenor/ugly (SettingsActivity), the saved file keeps it always.
  emit(SettingInfo::Enum(StrId::STR_SHELL_LEVEL, &CrossPointSettings::uiUglyLevel,
                        {StrId::STR_SHELL_LEVEL_PLAIN, StrId::STR_SHELL_LEVEL_AF}, "uiUglyLevel", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                          StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                        "sleepScreen", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                        "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                        {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                        StrId::STR_CAT_DISPLAY));
  // Labels in stored order; the screen lists them in its own (buildWakeTargetSetting).
  emit(SettingInfo::Enum(StrId::STR_WAKE_TARGET, &CrossPointSettings::wakeTarget,
                        {StrId::STR_UGLY_ROOT_SCREEN, StrId::STR_UGLY_START_BOOK, StrId::STR_WAKE_RESUME,
                         StrId::STR_UGLY_START_RECENT},
                        "wakeTarget", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_WAKE_NOTICE, &CrossPointSettings::wakeNotice, "wakeNotice",
                                  StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_SLEEP_BW_REFRESH, &CrossPointSettings::sleepBwFold, "sleepBwFold",
                                  StrId::STR_CAT_DISPLAY));
#if FREEINK_CAP_FRONTLIGHT
  emit(SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                          "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Value(StrId::STR_LIGHT_FROM_HOUR, &CrossPointSettings::frontlightFromHour, {0, 23, 1},
                          "frontlightFromHour", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Value(StrId::STR_LIGHT_UNTIL_HOUR, &CrossPointSettings::frontlightUntilHour, {0, 23, 1},
                          "frontlightUntilHour", StrId::STR_CAT_DISPLAY));
#endif
#if FREEINK_DEVICE_PICCO
  emit(SettingInfo::Enum(StrId::STR_SLIDE_SWITCH, &CrossPointSettings::slideSwitchAction,
                         {StrId::STR_FRONTLIGHT, StrId::STR_NIGHT_MODE, StrId::STR_TOUCH_READER_CONTROLS},
                         "slideSwitchAction", StrId::STR_CAT_DISPLAY));
#endif

  // --- Reader ---
  // Built-in font-family entry. Replaced per-call with a registry-aware
  // version when SD fonts are installed.
  emit(std::move(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
          .withTextSettings()));
  // Placeholder: the selectable sizes depend on the active font family, so
  // this entry is always replaced by buildFontSizeSetting() below. It only
  // fixes the setting's position in the Reader category.
  emit(std::move(SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings()));
  // Layout group, in the order the approved plan fixes: the four spacing
  // kinds first, then alignment, margin and paragraph indent. The style
  // group (embedded style, drop cap, hyphenation, ink weight, AA) follows.
  emit(std::move(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
                        {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT, StrId::STR_WIDE,
                         StrId::STR_VERY_WIDE, StrId::STR_LINE_SPACING_130, StrId::STR_LINE_SPACING_160},
#else
                        {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT, StrId::STR_WIDE,
                         StrId::STR_VERY_WIDE},
#endif
                        "lineSpacing", StrId::STR_CAT_READER)
          .withTextSettings()));
  emit(std::move(*getBaseTextSetting("letterSpacing")));
  emit(std::move(*getBaseTextSetting("wordSpacing")));
  emit(std::move(*getBaseTextSetting("extraParagraphSpacing")));
  emit(std::move(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE},
                        "paragraphAlignment", StrId::STR_CAT_READER)
          .withTextSettings()));
  emit(std::move(*getBaseTextSetting("screenMargin")));
  emit(std::move(*getBaseTextSetting("paragraphIndent")));
  emit(std::move(*getBaseTextSetting("embeddedStyle")));
  emit(std::move(SettingInfo::Enum(StrId::STR_DROP_CAP, &CrossPointSettings::dropCapMode,
                        {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_SPACING_LARGE}, "dropCapMode",
                        StrId::STR_CAT_READER)
          .withTextSettings()));
  emit(std::move(*getBaseTextSetting("hyphenationEnabled")));
  emit(SettingInfo::Enum(
          StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
          {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
          "orientation", StrId::STR_CAT_READER));

  emit(std::move(*getBaseTextSetting("readerInkWeight")));
  emit(std::move(*getBaseTextSetting("textAntiAliasing")));
  emit(SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                        "imageRendering", StrId::STR_CAT_READER));
  // Thanh trang thai trong trinh doc: sau muc, doc lap voi thanh ngoai.
  emit(SettingInfo::Enum(StrId::STR_HIDE_READER_STATUS_BAR, &CrossPointSettings::readerStatusBarMode,
                        {StrId::STR_STATE_OFF, StrId::STR_STATUS_BAR_CLOCK_BATTERY, StrId::STR_STATUS_BAR_DEFAULT,
                         StrId::STR_STATUS_BAR_CHAPTER_PROGRESS, StrId::STR_STATUS_BAR_CHAPTER_CLOCK,
                         StrId::STR_STATUS_BAR_CHAPTER_BATTERY, StrId::STR_STATUS_BAR_CLOCK_CHAPTER_PROGRESS,
                         StrId::STR_STATUS_BAR_BOOK_DETAILS},
                        "readerStatusBarMode", StrId::STR_CAT_READER));
  emit(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                        {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED, StrId::STR_NEXT_NEXT,
                         StrId::STR_PREV_PREV},
                        "sideButtonLayout", StrId::STR_CAT_READER));
  emit(SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                        {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}, "readerMenuStyle",
                        StrId::STR_CAT_READER));
  // --- Controls ---
  // touchReaderControls is a master toggle (#3586); direction gestures are
  // separate pageTurnGesture/previousPageGesture settings. All three are
  // touch-only (settingHiddenOnThisBoard hides them without a touch controller).
  emit(SettingInfo::Toggle(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                          "touchReaderControls", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Enum(StrId::STR_NEXT_PAGE_GESTURE, &CrossPointSettings::pageTurnGesture,
                        {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                         StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                        "pageTurnGesture", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Enum(StrId::STR_PREV_PAGE_GESTURE, &CrossPointSettings::previousPageGesture,
                        {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                         StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                        "previousPageGesture", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Enum(StrId::STR_BACK_TAP_ZONE, &CrossPointSettings::backTapZone,
                        {StrId::STR_PERCENT_15, StrId::STR_PERCENT_20, StrId::STR_PERCENT_25, StrId::STR_PERCENT_33},
                        "backTapZone", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Toggle(StrId::STR_READER_TAP_TIP, &CrossPointSettings::readerTapTip, "readerTapTip",
                                  StrId::STR_CAT_CONTROLS));
#if FREEINK_CAP_HAPTIC
  emit(SettingInfo::Enum(StrId::STR_VIBRATION, &CrossPointSettings::vibration,
                         {StrId::STR_VIBRATION_TOUCH, StrId::STR_VIBRATION_TOUCH_PAGE, StrId::STR_STATE_OFF}, "vibration",
                         StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Enum(StrId::STR_HAPTIC_INTENSITY, &CrossPointSettings::hapticIntensity,
                         {StrId::STR_HAPTIC_LOW, StrId::STR_HAPTIC_MEDIUM, StrId::STR_HAPTIC_HIGH}, "hapticIntensity",
                         StrId::STR_CAT_CONTROLS));
#endif
  // Persisted under the legacy "tapForReaderMenu" key: old saves map
  // 0 = Off, 1 = Tap.
  emit(SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                        {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}, "tapForReaderMenu",
                        StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                          "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Enum(StrId::STR_KEYBOARD_GEOMETRY, &CrossPointSettings::keyboardAligned,
                        {StrId::STR_KEYBOARD_STAGGERED, StrId::STR_KEYBOARD_ALIGNED}, "keyboardAligned",
                        StrId::STR_CAT_KEYBOARD));
  emit(SettingInfo::Toggle(StrId::STR_KEYBOARD_AXIS_SWAP, &CrossPointSettings::keyboardAxisSwapped,
                          "keyboardAxisSwapped", StrId::STR_CAT_KEYBOARD));
  emit(SettingInfo::String(StrId::STR_DEVICE_NAME, &SETTINGS.deviceName[0], sizeof(SETTINGS.deviceName), "deviceName"));
  emit(buildLongPressButtonSetting());
  emit(buildLongPressMenuSetting(hasTilt));
  // X4 Pro only; hidden elsewhere by settingHiddenOnThisBoard (#3089).
  emit(SettingInfo::Toggle(StrId::STR_DBL_CLICK_PWR_LIGHT, &CrossPointSettings::doubleClickPwrLight,
                          "doubleClickPwrLight", StrId::STR_CAT_CONTROLS));
  // Short power press and hard shake share one list of actions (QuickAction.h).
  emit(SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                        quickaction::powerLabels(), "shortPwrBtn", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                          "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS));
  emit(SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                          "backShortToFileBrowser", StrId::STR_CAT_CONTROLS));
  // Home button shortcuts (#3516): tap/double-tap/long-press, home-key boards only.
  for (unsigned i = 0; i < 3; ++i) {
    emit(SettingInfo::StaticEnum(home_button::GESTURE_LABELS[i], home_button::FIELDS[i],
                                        home_button::ACTION_LABELS, home_button::KEYS[i], StrId::STR_CAT_CONTROLS));
  }
  // --- Gestures (only with the QMI8658 IMU, X3) ---
  // Keys and values are the ones these rows had in Reader and Controls, so a saved
  // file reads the same here.
  if (savesMotion) {
    emit(SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                          // STR_INVERTED means inverted colours elsewhere; tilt needs a
                          // reversed direction, so it gets a word of its own.
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED}, "tiltPageTurn",
                          StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_TILT_TAB_NAVIGATION, &CrossPointSettings::tiltTabNavigation,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED},
                          "tiltTabNavigation", StrId::STR_CAT_MOTION));
    // Row tilt sits next to tab tilt: same band of gestures, other axis.
    emit(SettingInfo::Enum(StrId::STR_TILT_MENU_NAVIGATION, &CrossPointSettings::tiltMenuNavigation,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED},
                          "tiltMenuNavigation", StrId::STR_CAT_MOTION));
    // Flick strength per axis: side flicks turn pages and tabs, up/down
    // flicks move menu rows, and wrists differ on each.
    emit(SettingInfo::Enum(StrId::STR_TILT_STRENGTH_H, &CrossPointSettings::tiltStrengthH,
                          {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                          "tiltStrengthH", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_TILT_STRENGTH_V, &CrossPointSettings::tiltStrengthV,
                          {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                          "tiltStrengthV", StrId::STR_CAT_MOTION));
    // A hard shake, face down, face up and a double tap each run one of the power
    // button's actions on any screen, chosen from the shake's list (QuickAction.h).
    emit(SettingInfo::Enum(StrId::STR_SHAKE_ACTION, &CrossPointSettings::shakeAction,
                          quickaction::shakeLabels(), "shakeAction", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_SHAKE_STRENGTH, &CrossPointSettings::shakeStrength,
                          {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                          "shakeStrength", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_FACE_DOWN_ACTION, &CrossPointSettings::faceDownAction,
                          quickaction::shakeLabels(), "faceDownAction", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_FACE_UP_ACTION, &CrossPointSettings::faceUpAction,
                          quickaction::shakeLabels(), "faceUpAction", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_DOUBLE_TAP_ACTION, &CrossPointSettings::doubleTapAction,
                          quickaction::shakeLabels(), "doubleTapAction", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_SCREEN_TAP_ACTION, &CrossPointSettings::screenTapAction,
                          quickaction::shakeLabels(), "screenTapAction", StrId::STR_CAT_MOTION));
    emit(SettingInfo::Enum(StrId::STR_EDGE_TAP_ACTION, &CrossPointSettings::edgeTapAction,
                          quickaction::shakeLabels(), "edgeTapAction", StrId::STR_CAT_MOTION));
  }

  // Persisted here; set from the Connectivity, Plugins popup. Category-less and kept off the web page.
  emit(SettingInfo::Toggle(StrId::STR_PLUGINS, &CrossPointSettings::pluginsEnabled, "pluginsEnabled").withWebHidden());
  emit(SettingInfo::Toggle(StrId::STR_PLUGIN_HUB, &CrossPointSettings::pluginHubPromptHidden, "pluginHubPromptHidden")
           .withWebHidden());

  // --- System ---
  emit(SettingInfo::Value(
          StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
          {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
          "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
  emit(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                          StrId::STR_CAT_SYSTEM));
  // The File order of the running shell, kept in file-browser.json beside its last folder.
  emit(SettingInfo::DynamicEnum(
      StrId::STR_FILE_SORT, {StrId::STR_FILE_SORT_NAME, StrId::STR_FILE_SORT_MODIFIED, StrId::STR_FILE_SORT_SIZE},
      [] { return static_cast<uint8_t>(filebrowser::sortModeValue(filebrowser::settingsSort())); },
      [](const uint8_t v) { filebrowser::chooseSettingsSort(filebrowser::sortModeFromValue(v)); },
      nullptr, StrId::STR_CAT_SYSTEM));
  emit(SettingInfo::Toggle(StrId::STR_LIBRARY_USE_METADATA, &CrossPointSettings::libraryUseMetadata,
                          "libraryUseMetadata", StrId::STR_CAT_SYSTEM));
  emit(SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                          "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM));
  emit(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                          "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM));

  // OPDS download folder: persisted + web-exposed, but category-less so it
  // is hidden from the on-device Settings screen (edited via OPDS UI).
  emit(SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                          sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"));
  // OPDS download filename format: persisted + web-exposed, category-less so it
  // is hidden from the on-device Settings screen (cycled from the OPDS UI).
  emit(SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                        {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                        "opdsFilenameFormat"));

  // The sleep screen tenor/ugly took over: persisted only, category-less (so not on the device) and kept
  // off the web page by getSettingsList().
  emit(SettingInfo::Value(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellSleepMemo,
                         {0, CrossPointSettings::SLEEP_SCREEN_MODE_COUNT, 1}, "uiShellSleepMemo"));
  emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellClockMemo, "uiShellClockMemo"));
  emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellClockOnce, "uiShellClockOnce"));
  emit(SettingInfo::Value(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellFontMemo, {0, 3, 1}, "uiShellFontMemo"));
  emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uglyBatteryHidden, "uglyBatteryHidden"));

  // Frontlight quick-panel state: persisted and web-exposed, but hidden
  // from the on-device Settings screen because the swipe panel owns it.
  emit(SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                         "frontlightBrightness"));
#if FREEINK_CAP_WARMLIGHT
  emit(SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"));
#endif
  emit(SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"));

  // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
  emit(SettingInfo::DynamicString(
      StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
      [](const std::string& v) {
        KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
        KOREADER_STORE.saveToFile();
      },
      "koUsername", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicString(
           StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
           [](const std::string& v) {
             KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
             KOREADER_STORE.saveToFile();
           },
           "koPassword", StrId::STR_KOREADER_SYNC)
           .withObfuscated());
  emit(SettingInfo::DynamicString(
      StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
      [](const std::string& v) {
        KOREADER_STORE.setServerUrl(v);
        KOREADER_STORE.saveToFile();
      },
      "koServerUrl", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicString(
      StrId::STR_KOREADER_CUSTOM_HEADER_1_NAME, [] { return KOREADER_STORE.getCustomHeaders()[0].name; },
      [](const std::string& v) {
        KOREADER_STORE.setCustomHeader(0, v, KOREADER_STORE.getCustomHeaders()[0].value);
        KOREADER_STORE.saveToFile();
      },
      "koHeader1Name", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicString(
           StrId::STR_KOREADER_CUSTOM_HEADER_1_VALUE, [] { return KOREADER_STORE.getCustomHeaders()[0].value; },
           [](const std::string& v) {
             KOREADER_STORE.setCustomHeader(0, KOREADER_STORE.getCustomHeaders()[0].name, v);
             KOREADER_STORE.saveToFile();
           },
           "koHeader1Value", StrId::STR_KOREADER_SYNC)
           .withObfuscated());
  emit(SettingInfo::DynamicString(
      StrId::STR_KOREADER_CUSTOM_HEADER_2_NAME, [] { return KOREADER_STORE.getCustomHeaders()[1].name; },
      [](const std::string& v) {
        KOREADER_STORE.setCustomHeader(1, v, KOREADER_STORE.getCustomHeaders()[1].value);
        KOREADER_STORE.saveToFile();
      },
      "koHeader2Name", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicString(
           StrId::STR_KOREADER_CUSTOM_HEADER_2_VALUE, [] { return KOREADER_STORE.getCustomHeaders()[1].value; },
           [](const std::string& v) {
             KOREADER_STORE.setCustomHeader(1, KOREADER_STORE.getCustomHeaders()[1].name, v);
             KOREADER_STORE.saveToFile();
           },
           "koHeader2Value", StrId::STR_KOREADER_SYNC)
           .withObfuscated());
  emit(SettingInfo::DynamicEnum(
      StrId::STR_SERVER_TYPE, {StrId::STR_CROSSPOINT_SERVER, StrId::STR_KOSYNC_SERVER, StrId::STR_OTHER},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getServerType()); },
      [](uint8_t v) {
        KOREADER_STORE.setServerType(static_cast<KOReaderServerType>(v));
        KOREADER_STORE.saveToFile();
      },
      "koServerType", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicEnum(
      StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
      [](uint8_t v) {
        KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
        KOREADER_STORE.saveToFile();
      },
      "koMatchMethod", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicEnum(
      StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
      [](uint8_t v) {
        KOREADER_STORE.setSendMetadata(v != 0);
        KOREADER_STORE.saveToFile();
      },
      "koSendMetadata", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicEnum(
      StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
      [](uint8_t v) {
        KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
        KOREADER_STORE.saveToFile();
      },
      "koSyncBehavior", StrId::STR_KOREADER_SYNC));
  emit(SettingInfo::DynamicEnum(
      StrId::STR_SYNC_CLIPPINGS, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
      [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncClippings()); },
      [](uint8_t v) {
        if (KOREADER_STORE.getSyncClippings() == (v != 0)) return;
        KOREADER_STORE.setSyncClippings(v != 0);
        KOREADER_STORE.saveToFile();
      },
      "koSyncClippings", StrId::STR_KOREADER_SYNC));
  // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
  emit(SettingInfo::DynamicEnum(StrId::STR_READER_STATUS_TOP,
        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_STATE_OFF},
        [] { return SETTINGS.readerStatusItem(0); }, [](uint8_t v) { SETTINGS.setReaderStatusItem(0, v); },
        "readerStatusTop", StrId::STR_CUSTOMISE_STATUS_BAR));
  for (int row = 1; row <= 3; ++row) {
    const StrId names[] = {StrId::STR_READER_STATUS_LEFT, StrId::STR_READER_STATUS_CENTER,
                           StrId::STR_READER_STATUS_RIGHT};
    const char* keys[] = {"readerStatusLeft", "readerStatusCenter", "readerStatusRight"};
    std::vector<StrId> values = {StrId::STR_STATE_OFF, StrId::STR_CLOCK, StrId::STR_BATTERY,
        StrId::STR_CHAPTER_PAGE_COUNT, StrId::STR_BOOK_PROGRESS_PERCENTAGE,
        StrId::STR_READER_STATUS_CHAPTER_ETA, StrId::STR_READER_STATUS_BOOK_ETA};
    if (row == 2) {
      values.push_back(StrId::STR_CHAPTER_NAME);
      values.push_back(StrId::STR_READER_STATUS_CHAPTER_AND_BOOK_PROGRESS);
    }
    emit(SettingInfo::DynamicEnum(names[row - 1], std::move(values),
          [row] { return SETTINGS.readerStatusItem(row); },
          [row](uint8_t v) { SETTINGS.setReaderStatusItem(row, v); }, keys[row - 1], StrId::STR_CUSTOMISE_STATUS_BAR));
  }
  emit(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                          "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                          "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                        {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                        "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                        {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  // Clock entries (persistence + web settings; the device UI is
  // ClockSettingsActivity under System settings).
  emit(SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, std::move(statusBarClockValues),
                        "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_HOME_STYLE, &CrossPointSettings::homeStyle,
                        {StrId::STR_HOME_STYLE_STATS, StrId::STR_HOME_STYLE_LARGE, StrId::STR_HOME_STYLE_SHELF},
                        "homeStyle", StrId::STR_CAT_DISPLAY));
  emit(SettingInfo::Toggle(StrId::STR_CLOCK_AUTO_TIMEZONE, &CrossPointSettings::clockAutoTimezone, "clockAutoTimezone",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
  // LEGACY: retired quarter-hour UTC offset (biased by 48), still written so
  // timezones::activeIndex() can migrate it into clockTimezone (#3562).
  emit(SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                         "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR));
  // Index into the append-only table in src/util/Timezones.cpp; 255 = unset.
  emit(SettingInfo::Value(StrId::STR_TIMEZONE, &CrossPointSettings::clockTimezone, {0, 255, 1}, "clockTimezone",
                         StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_CLOCK_DST, &CrossPointSettings::clockDst,
                        {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, "clockDst",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_CLOCK_IN_HEADER, &CrossPointSettings::clockShowInHeader,
                        {StrId::STR_HIDE, StrId::STR_CLOCK_HEADER_TIME, StrId::STR_CLOCK_HEADER_TIME_DATE}, "clockShowHeader",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                        {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                        StrId::STR_CUSTOMISE_STATUS_BAR));
  // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
  // on next WiFi connect, which is useful when crossing time zones.
  emit(SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
  emit(SettingInfo::Enum(StrId::STR_UGLY_ROOT_SCREEN, &CrossPointSettings::uglyRootScreen,
                        {StrId::STR_UGLY_START_DIARY, StrId::STR_UGLY_START_DESK},
                        "uglyRootScreen", StrId::STR_CAT_DISPLAY));
}
