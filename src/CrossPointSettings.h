#pragma once

#include <ArduinoJson.h>
#include <BlePageTurner.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/ReaderSpacing.h>
#include <PersistableStore.h>

#include <cstdint>

#include "util/HomeButtonInput.h"

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton. A card with no settings file runs on what this
  // leaves behind, so the tenor/cross setup goes over the upstream initializers below.
  // Out of line on purpose: inline, the member-by-member initialization was copied into
  // each function that inlined getInstance(), about 4 KB of flash.
  CrossPointSettings();

  friend class PersistableStore<CrossPointSettings>;

 public:
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    COVER_CUSTOM = 4,
    BLANK = 5,
    QUICK_RESUME = 6,
    TRANSPARENT_CUSTOM = 7,
    // Man ngu mac dinh cua tenor/cross, nen thang vao firmware. THEM VAO CUOI, vi so thu
    // tu nay duoc luu xuong settings.json: chen vao giua la moi ban ghi cu hieu nham.
    TENOR = 8,
    STATS = 9,
    // A random saved quote with the small cover of its book (v1.0.11).
    QUOTE = 10,
    // The tenor/ugly doodle and a line of abuse (the default sleep screen of that shell).
    UGLY = 11,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR {
    BOOK_PROGRESS = 0,
    CHAPTER_PROGRESS = 1,
    HIDE_PROGRESS = 2,
    STATUS_BAR_PROGRESS_BAR_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR_THICKNESS {
    PROGRESS_BAR_THIN = 0,
    PROGRESS_BAR_NORMAL = 1,
    PROGRESS_BAR_THICK = 2,
    STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
  };
  enum STATUS_BAR_TITLE { BOOK_TITLE = 0, CHAPTER_TITLE = 1, HIDE_TITLE = 2, STATUS_BAR_TITLE_COUNT };
  enum XTC_STATUS_BAR_MODE {
    XTC_STATUS_BAR_HIDE = 0,
    XTC_STATUS_BAR_BOTTOM = 1,
    XTC_STATUS_BAR_TOP = 2,
    XTC_STATUS_BAR_MODE_COUNT
  };

  enum STATUS_BAR_CLOCK_MODE {
    STATUS_BAR_CLOCK_HIDE = 0,
    STATUS_BAR_CLOCK_RIGHT = 1,
    STATUS_BAR_CLOCK_LEFT = 2,
    STATUS_BAR_CLOCK_MODE_COUNT
  };

  // Auto follows the timezone's baked DST rule; On/Off override it - the
  // escape hatch for a zone whose law changed before the firmware caught up.
  enum CLOCK_DST_MODE { CLOCK_DST_AUTO = 0, CLOCK_DST_ON = 1, CLOCK_DST_OFF = 2, CLOCK_DST_MODE_COUNT };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Up = Previous, Down = Next. NEXT_NEXT and PREV_PREV assign both
  // buttons to the same direction for one-handed reading. Both are appended
  // rather than slotted in beside their siblings: the value is persisted as a
  // number, so inserting would silently change what an existing save means.
  enum SIDE_BUTTON_LAYOUT {
    PREV_NEXT = 0,
    NEXT_PREV = 1,
    SIDE_BUTTONS_DISABLED = 2,
    NEXT_NEXT = 3,
    PREV_PREV = 4,
    SIDE_BUTTON_LAYOUT_COUNT
  };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName)
  enum FONT_FAMILY { NOTOSERIF = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t LEGACY_OPENDYSLEXIC = 2;
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Reader font size is a point size, not an enum slot - see fontPointSize.
  // Legacy 1.4-and-earlier files stored a 0..3 SMALL/MEDIUM/LARGE/EXTRA_LARGE
  // slot; fromJson() folds that range up (see LEGACY_FONT_SIZE_MAX).
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = 3;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 16;
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes).
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_NEVER = 5,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions. PWR_CONFIRM is only offered on touch
  // boards (see SettingsList.h).
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    FOOTNOTES = 4,
    PWR_CONFIRM = 5,
    BACK = 6,  // Stored by value: new actions go last (quickaction::CHOICES)
    READER_MENU = 7,
    SAVE_QUOTE = 8,
    SHORT_PWRBTN_COUNT
  };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by number: a new function takes the next number after the last one and
  // is appended at the END of this enum, otherwise stored numbers shift and existing saves are silently
  // misinterpreted. The row in SettingsList.h maps list position to number (buildLongPressMenuSetting).
  enum LONG_PRESS_MENU_FUNCTION {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_READER_MENU = 4,
    LP_MENU_FILE_TRANSFER = 5,
    // Boards without an IMU leave this one out of the list; its number stays reserved.
    LP_MENU_TILT_PAGE_TURN = 6,
    // Runs the reader menu's Save quotation (the quote selector). Appended after the tilt toggle, so
    // every earlier save reads as before; the list shows it at position 6 on a board without an IMU.
    LP_MENU_SAVE_QUOTE = 7,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long press behavior
  // Mặc định theo quyết định của v1.0.3: giữ nút bên cạnh = nhảy chương.
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,
    ORIENTATION_CHANGE = 2,
    FONT_SIZE_STEP = 3,  // giu nut tien = co chu to mot nac, giu nut lui = nho mot nac (14/09/2026)
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  enum UI_TEXT_SIZE { UI_TEXT_SMALL = 0, UI_TEXT_MEDIUM = 1, UI_TEXT_LARGE = 2, UI_TEXT_SIZE_COUNT };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // How Select opens the reader menu: the classic full-screen list, or a toolbar
  // overlay (top/bottom bars with Contents / Text / More bottom-sheet panels)
  // painted over the page.
  enum READER_MENU_STYLE { READER_MENU_LIST = 0, READER_MENU_TOOLBAR = 1, READER_MENU_STYLE_COUNT };

  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_NVERTED = 2, TILT_PAGE_TURN_COUNT };

  // How hard a wrist flick must be before it counts, per axis. Persisted by index.
  enum TILT_STRENGTH {
    TILT_STRENGTH_LIGHT = 0,
    TILT_STRENGTH_MEDIUM = 1,
    TILT_STRENGTH_STRONG = 2,
    TILT_STRENGTH_COUNT
  };

  // touchReaderControls is now a master on/off (#3586); which gesture turns
  // the page in each direction lives in the two PAGE_TURN_GESTURE fields
  // below. Touch boards only (X3/X4 have no touch controller and hide this
  // row entirely - see settingHiddenOnThisBoard).
  enum TOUCH_READER_CONTROLS { TOUCH_READER_OFF = 0, TOUCH_READER_ON = 1, TOUCH_READER_CONTROLS_COUNT };

  // Per-direction reader page-turn gestures. INVERTED_TAP is tap-only; either
  // direction set to it swaps both directions' shared tap zones (see
  // ReaderUtils::detectTouchPageTurn).
  enum PAGE_TURN_GESTURE {
    TAP_AND_SWIPE = 0,
    TAP_ONLY = 1,
    SWIPE_ONLY = 2,
    INVERTED_TAP = 3,
    PAGE_TURN_GESTURE_DISABLED = 4,
    PAGE_TURN_GESTURE_COUNT
  };

  // How the reader menu opens on touch boards. Persisted under the legacy
  // "tapForReaderMenu" key: 0/1 keep their old Off/Tap meaning.
  enum SHOW_READER_MENU { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2, SHOW_READER_MENU_COUNT };

  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Sleep screen settings
  // tenor/cross bay an pham cua chinh no khi ngu, khong bat ai phai chep file vao the.
  uint8_t sleepScreen = TENOR;
  // Night mode: inverted output polarity, applied to every activity per
  // render by ActivityManager. The sleep screen opts out itself.
  uint8_t screenInverted = 0;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Thanh trang thai ngoai trinh doc: dung ba muc da chot. Gia tri luu xuong
  // settings.json nen CHI THEM VAO CUOI.
  enum GLOBAL_STATUS_BAR_MODE {
    GLOBAL_STATUS_BAR_SMALL = 0,  // Mac dinh nho
    GLOBAL_STATUS_BAR_OFF = 1,    // Tat: khong pin, khong gio, khong nhan nut
    GLOBAL_STATUS_BAR_LARGE = 2,  // Lon
    GLOBAL_STATUS_BAR_MODE_COUNT
  };
  // Thanh trang thai trong trinh doc: sau muc, doc lap voi lua chon ngoai trinh doc.
  enum READER_STATUS_BAR_MODE {
    READER_STATUS_BAR_OFF = 0,               // Tat
    READER_STATUS_BAR_CLOCK_BATTERY = 1,     // Dong ho & pin
    READER_STATUS_BAR_DEFAULT = 2,           // Mac dinh du
    READER_STATUS_BAR_CHAPTER_PROGRESS = 3,  // Ten chuong & tien trinh chuong
    READER_STATUS_BAR_CHAPTER_CLOCK = 4,     // Ten chuong & dong ho
    READER_STATUS_BAR_CHAPTER_BATTERY = 5,   // Ten chuong & pin
    READER_STATUS_BAR_MODE_COUNT
  };
  // Trang thai thanh trang thai
  uint8_t globalStatusBarMode = GLOBAL_STATUS_BAR_SMALL;
  uint8_t readerStatusBarMode = READER_STATUS_BAR_DEFAULT;
  // Hai co cua v1.0.2 chi con de doc file cu roi quy doi trong fromJson.
  uint8_t hideReaderStatusBar = 0;
  uint8_t hideGlobalStatusBar = 0;
  bool globalStatusBarHidden() const { return globalStatusBarMode == GLOBAL_STATUS_BAR_OFF; }
  bool globalStatusBarLarge() const { return globalStatusBarMode == GLOBAL_STATUS_BAR_LARGE; }
  bool readerStatusBarHidden() const { return readerStatusBarMode == READER_STATUS_BAR_OFF; }
  // Hide Battery %: the one place that says whether the number beside the battery is drawn.
  bool batteryPercentShown(const bool inReader) const {
    return hideBatteryPercentage == HIDE_NEVER || (hideBatteryPercentage == HIDE_READER && !inReader);
  }
  // The chapter name (statusBarTitle), chapter page count and book percentage each
  // have their own switch; Off, battery and clock stay with readerStatusBarMode.
  // statusBarItemsMode names the mode the three switches belong to. The reader menu
  // and the web page write the mode byte directly, so a mode picked there shows
  // what its name says until the switches adopt it.
  uint8_t statusBarChapterPageCount = 1;
  uint8_t statusBarItemsMode = READER_STATUS_BAR_DEFAULT;
  // Makes the three switches hold what the current mode shows, before one is flipped.
  void adoptReaderStatusItems();
  uint8_t statusBarBookProgressPercentage = 1;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  uint8_t statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  uint8_t statusBarTitle = CHAPTER_TITLE;
  uint8_t statusBarBattery = 1;
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // Clock display in status bar (any board whose RTC probe succeeds)
  uint8_t statusBarClock = STATUS_BAR_CLOCK_HIDE;
  // LEGACY, kept for migration only: quarter-hour UTC offset biased by 48
  // (48 = UTC+0). Superseded by clockTimezone; read once by
  // timezones::activeIndex() when clockTimezone is unset.
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Tenor: resolve clockUtcOffsetQ from network lookup (src/network/TimezoneLookup.cpp)
  // instead of a manual offset. Independent of upstream's named-timezone picker below.
  uint8_t clockAutoTimezone = 1;
  // Index into the timezone table (src/util/Timezones.cpp, append-only).
  // 255 = never chosen; falls back to the legacy UTC offset, then UTC.
  uint8_t clockTimezone = 255;
  // CLOCK_DST_MODE: follow the zone's DST rule, or force it on/off.
  uint8_t clockDst = CLOCK_DST_AUTO;
  // Show the clock opposite the battery in every header band that draws one.
  uint8_t clockShowInHeader = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings. Each of these four is a readerSpacing::Level, not
  // a private enum: the ordinal is what settings.json stores, and ReaderSpacing.h
  // turns it into the line factor, the letter delta and the paragraph gap.
  // toJson stamps textSpacingVersion 3; fromJson folds v1/v2 files into it.
  //
  // Upstream's own word/character spacing redesign (percent-scale wordSpacing,
  // separate characterSpacing 0..4 offset) is NOT taken here: it retypes the
  // "wordSpacing" JSON key from a 5-level ordinal to a 50..200 percent, which
  // would misread every existing save. Tenor's unified Level-based scheme
  // covers the same UX (letterSpacing already is upstream's characterSpacing
  // idea, just under Tenor's own key) so no capability is lost.
  uint8_t extraParagraphSpacing = readerSpacing::LEVEL_DEFAULT;
  uint8_t letterSpacing = readerSpacing::LEVEL_DEFAULT;
  // readerSpacing::Level applied to the U+0020 advance. Joined the group at
  // textSpacingVersion 3 and no earlier release wrote the key, so an absent key
  // keeps the default rather than being folded like the three above.
  uint8_t wordSpacing = readerSpacing::LEVEL_DEFAULT;
  // 0 = off, 1 = default, 2 = wide.
  uint8_t paragraphIndent = 1;
  uint8_t textAntiAliasing = 1;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // X4 Pro: double-click power toggles the frontlight. Disabling frees the
  // power button for shortPwrBtn actions without the double-click wait.
  uint8_t doubleClickPwrLight = 1;
  uint8_t homeButtonTapAction = static_cast<uint8_t>(HomeButtonAction::Home);
  // Unassigned: a double tap action makes every single tap wait 350 ms to see whether a second follows.
  uint8_t homeButtonDoubleTapAction = static_cast<uint8_t>(HomeButtonAction::Ignore);
  uint8_t homeButtonLongPressAction = static_cast<uint8_t>(HomeButtonAction::ReaderMenu);
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonFollowOrientation = 0;
  uint8_t keyboardAxisSwapped = 1;
  uint8_t keyboardAligned = 1;
  // Ten thiet bi do nguoi dung dat, dung cho hostname Wi-Fi, ten mDNS va ten
  // diem phat. De trong thi giu ten mac dinh. Doc qua deviceNetworkName().
  // Tab Yeu thich cua menu doc: cac muc nguoi doc tu ghim, THEO DUNG THU TU ho xep.
  // Giu o day de no song qua lan tat may. Moi o la mot readermenu::Action, luu bang so
  // chu khong bang ten, nen them muc moi phai THEM VAO CUOI enum do (xem ReaderMenuLayout.h).
  // Tran 8 phai khop readermenu::TOI_DA_GHIM; CrossPointSettings.cpp co static_assert giu.
  static constexpr uint8_t READER_FAVORITE_MAX = 8;
  uint8_t readerFavorites[READER_FAVORITE_MAX] = {};
  uint8_t readerFavoriteCount = 0;
  // Nguoi doc da tung tu xep danh sach nay chua. Phai tach khoi readerFavoriteCount == 0,
  // vi danh sach RONG la mot lua chon that: go het moi muc ra thi tab Yeu thich phai
  // rong, chu khong tu moc lai ban mac dinh o lan mo sau.
  uint8_t readerFavoritesDaDat = 0;

  char deviceName[32] = "";
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = NOTOSERIF;
  // Point size of the reader font. Only sizes the active family actually ships
  // are selectable; SdCardFontSystem::ensureLoaded() snaps this to the nearest
  // available size (and persists the snap) whenever the family changes.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  // User preference; a family without the variant temporarily renders at weight 0.
  uint8_t readerInkWeight = 0;
  // readerSpacing::Level. Was LINE_COMPRESSION TIGHT/NORMAL/WIDE before
  // textSpacingVersion 3; fromJson maps those old ordinals onto the levels.
  uint8_t lineSpacing = readerSpacing::LEVEL_DEFAULT;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;

  // Reader screen margin settings
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 40;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;
  uint8_t screenMargin = SCREEN_MARGIN_MIN;
  // OPDS download destination folder ("" = SD root). Global; edited from the
  // OPDS server list. Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen.
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Persisted via a category-less SettingInfo::Enum,
  // edited from the OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Long-press page turn button behavior
  uint8_t longPressButtonBehavior = CHAPTER_SKIP;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Defaults to Disabled so shortcut-based bookmark toggling remains opt-in.
  uint8_t longPressMenuFunction = LP_MENU_DISABLED;
  // UI font tier. Persisted ordinals: 0 Small, 1 Medium, 2 Large.
  uint8_t uiTextSize = UI_TEXT_SMALL;
  // Which shell draws Home: 0 tenor/cross, 1 tenor/ugly. A settings file without the key loads as 0.
  uint8_t uiShell = 0;
  // How ugly tenor/ugly draws its letters: 0 ugly (straight), 1 ugly af (turned as they are drawn). A settings file without the key loads as 1.
  uint8_t uiUglyLevel = 1;
  // The sleep screen tenor/ugly took over when the user went in, as that screen + 1 (0 = none taken). Leaving
  // gives it back. Persisted in settings.json, never shown on the device or the web page.
  uint8_t uiShellSleepMemo = 0;
  // Tenor-only appearance preferences; existing themes retain their own hints.
  uint8_t tenorButtonSymbols = 1;
  uint8_t tenorSideArrows = 1;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Drop cap on the opening character of the first paragraph in a chapter:
  // 0 Off, 1 Default, 2 Large. Older files stored a boolean under
  // focusReadingEnabled and are migrated on read.
  uint8_t dropCapMode = readerSpacing::DROP_CAP_DEFAULT;
  uint8_t readerMenuStyle = READER_MENU_LIST;
  // SD card font family name (empty = use built-in fontFamily).
  // Bokerlam is the reader font this firmware ships as its default choice. It lives on the
  // card, not in flash, so a card without /.fonts/Bokerlam falls back on its own:
  // SdCardFontSystem::begin() clears the name and the built-in fontFamily takes over.
  // A settings file that already exists wins over this - loadFromFile() reads the stored
  // name (blank included), so nobody's saved font choice gets overwritten by an upgrade.
  char sdFontFamilyName[32] = "Bokerlam";
  // Dictionary folder name under /dictionaries (empty = no dictionary)
  char dictionaryName[32] = "";
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Show the title and author read from inside each book rather than its
  // filename. Users can disable this to make index rebuilds skip EPUB parsing.
  uint8_t libraryUseMetadata = 1;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Move epub to /Read/ folder on SD card when finished (0 = disabled, 1 = enabled)
  uint8_t moveFinishedToReadFolder = 0;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  uint8_t backShortToFileBrowser = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Tilt-based page turning (X3 only - requires QMI8658 IMU)
  uint8_t tiltPageTurn = TILT_OFF;
  // Tilt-based tab navigation (X3 only - requires QMI8658 IMU)
  uint8_t tiltTabNavigation = TILT_OFF;
  // Tilt-based row navigation on menu screens (X3 only - requires QMI8658 IMU)
  uint8_t tiltMenuNavigation = TILT_OFF;
  // Flick strength for the side (page turn, tabs) and up/down (menu rows) axes.
  uint8_t tiltStrengthH = TILT_STRENGTH_MEDIUM;
  uint8_t tiltStrengthV = TILT_STRENGTH_MEDIUM;
  // The mode a quick toggle turns tilt page turn back on to, so an Inverted
  // choice survives being switched off and on again.
  uint8_t tiltPageTurnLastOn = TILT_NORMAL;
  // Hard shake action, and how hard the shake must be (a TILT_STRENGTH).
  uint8_t shakeAction = 0;  // Off; a place in quickaction::SHAKE_ORDER
  uint8_t shakeStrength = TILT_STRENGTH_MEDIUM;
  // Face down and face up actions, each a place in quickaction::SHAKE_ORDER (0 = Off).
  uint8_t faceDownAction = 0;
  uint8_t faceUpAction = 0;
  // Double tap on the back, on the screen and on a side edge, each a place in
  // quickaction::SHAKE_ORDER (0 = Off). The key doubleTapAction predates the other two and stays.
  uint8_t doubleTapAction = 0;
  uint8_t screenTapAction = 0;
  uint8_t edgeTapAction = 0;
  // Master reader-touch toggle on boards with a touch controller.
  uint8_t touchReaderControls = TOUCH_READER_ON;
  // Which gestures turn the page in each direction (PAGE_TURN_GESTURE).
  uint8_t pageTurnGesture = TAP_AND_SWIPE;
  uint8_t previousPageGesture = TAP_AND_SWIPE;
  // Width of the back-tap column on a reader page, a step of readertap::BACK_PERCENT (15/20/25/33%).
  uint8_t backTapZone = 2;
  // Reader menu open gesture (SHOW_READER_MENU: off / center tap / bottom-edge
  // up-swipe). Only surfaced on home-key boards, where Home is the capacitive
  // key and the bottom edge is free; elsewhere it stays at the Tap default.
  uint8_t showReaderMenu = READER_MENU_TAP;
  // Frontlight quick-panel state. Category-less SettingsList entries persist
  // these without adding them to the regular Settings screen.
  uint8_t frontlightBrightness = 60;
  uint8_t frontlightWarmth = 50;  // 0 = cool .. 100 = warm
  uint8_t frontlightOn = 0;
  // Restore the saved on/off state after a normal boot or wake. Brightness and
  // warmth are always remembered even when this is disabled.
  uint8_t frontlightRestoreOnWake = 1;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Keyboard layouts the user can reach, using keyboard_layouts::ALL table bits.
  // 0 means "not configured", resolved to the UI language's layout plus English.
  // Any other value is an explicit choice and is used as-is: the language of the
  // books someone reads is not necessarily the language of their UI.
  // See keyboard_layouts:: for the bit assignment and the defaulting rules.
  uint16_t keyboardLayouts = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;
  // Wake straight back into the book that was open when deep sleep began,
  // instead of returning to Home. 0 = off, 1 = on.
  uint8_t wakeIntoBook = 0;
  // X3 UC8279: a wake to Home from a kept sleep frame draws the "Starting up" notice over the sleep
  // screen first (measured +270 ms to Home done). 0 = off (default), 1 = on.
  uint8_t wakeNotice = 0;
  // X3 only: end every sleep screen on one full black and white refresh, gray art as an
  // ordered dither (v1.0.12). 0 = gray waveforms, real 4-level gray (default since v1.0.52),
  // 1 = fold to black and white. Stored as "sleepBwFold": the v1.0.12 to v1.0.51 key
  // "sleepBwRefresh" held 1 on every card by default, and is no longer read.
  uint8_t sleepBwFold = 0;

  // --- BLE page turner (BTH2) -------------------------------------------------
  // Mac dinh TAT. Luu tay trong toJson/fromJson (khong qua SettingsList) vi man
  // hinh cua no la BlePageTurnerActivity, khong phai man Cai dat chung. Cac khoa
  // JSON va luat doc/ghi nam o lib/BlePageTurner (BlePageTurnerJson.h), nen file
  // cai dat cu doc len y nhu truoc.
  bleturner::Config ble;

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  // Whether a power-button wake may boot. With Short Power Button = Sleep a tap
  // puts the device to sleep, so a tap must also wake it, on every board;
  // otherwise the press must have been held through verification.
  static bool acceptPowerWake(const uint8_t shortPwrBtnSetting, const bool holdVerified) {
    return holdVerified || shortPwrBtnSetting == SLEEP;
  }

  // A power key that reaches the firmware only as a click (the Paper Mono PMIC) has no held
  // press to sleep on, so a click sleeps under Sleep and under Ignore; any other action wins.
  static constexpr bool powerClickSleeps(const uint8_t shortPwrBtnSetting) {
    return shortPwrBtnSetting == SLEEP || shortPwrBtnSetting == IGNORE;
  }

  // The tenor/cross reading setup (v1.0.14), the one list of its fields and values. It is
  // the default of a new card, and fromJson moves a file without the stamp onto it once,
  // after the older version folds so none of them rewrites a value set here. Language,
  // Bluetooth, auto-sleep and the button map are left to the owner.
  static constexpr uint8_t TENOR_PRESET_VERSION = 1;
  // Force refresh on the power key, except where the key is a click that sleeps only under
  // Ignore (powerClickSleeps): Paper Mono keeps Ignore, or its key could never sleep.
  static constexpr uint8_t tenorPresetPowerAction() {
#if defined(FREEINK_DEVICE_PAPERMONO) && FREEINK_DEVICE_PAPERMONO
    return IGNORE;
#else
    return FORCE_REFRESH;
#endif
  }
  void applyTenorPreset() {
    extraParagraphSpacing = readerSpacing::VERY_NARROW;
    lineSpacing = readerSpacing::NARROW;
    wordSpacing = readerSpacing::WIDE;
    paragraphIndent = 2;  // wide
    readerInkWeight = 1;
    frontButtonFollowOrientation = 1;
    shortPwrBtn = tenorPresetPowerAction();
    sleepScreen = QUOTE;
    statusBarClock = STATUS_BAR_CLOCK_RIGHT;
    tiltMenuNavigation = TILT_NORMAL;
    tiltPageTurn = TILT_NORMAL;
    tiltStrengthV = TILT_STRENGTH_LIGHT;
    tiltTabNavigation = TILT_NVERTED;
  }

  // Tilt page turn quick toggle: Off goes back to the last mode that was on.
  void toggleTiltPageTurn() {
    if (tiltPageTurn != TILT_OFF) {
      tiltPageTurnLastOn = tiltPageTurn;
      tiltPageTurn = TILT_OFF;
    } else {
      tiltPageTurn = tiltPageTurnLastOn == TILT_NVERTED ? TILT_NVERTED : TILT_NORMAL;
    }
  }

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships - otherwise the settings UI would keep
  // offering a size nothing renders at. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved status-bar composition. Consumers read the spec; only settings
  // editors read the raw fields.
  //
  // Deliberately NOT built under storeMutex: every field it reads is a single
  // byte, so a concurrent settings write can never produce a corrupt value -
  // only a snapshot mixing pre- and post-change fields. That costs at most one
  // e-ink frame drawn with a mixed status bar, which self-corrects on the next
  // refresh. Locking here would instead put a mutex on the render path and
  // stall it behind the SD write inside saveToFile(). Don't add one back.
  struct StatusBarSpec {
    bool showChapterPageCount = false;
    bool showBookProgressPercent = false;
    uint8_t titleMode = HIDE_TITLE;  // STATUS_BAR_TITLE
    bool showBattery = false;
    bool showBatteryPercent = false;
    uint8_t clockMode = STATUS_BAR_CLOCK_HIDE;  // STATUS_BAR_CLOCK_MODE
    bool clock12h = false;
    uint8_t progressBarMode = HIDE_PROGRESS;  // STATUS_BAR_PROGRESS_BAR
    uint8_t progressBarHeightPx = 0;          // (thickness+1)*2; 0 when the bar is hidden
    uint8_t xtcMode = XTC_STATUS_BAR_HIDE;    // XTC_STATUS_BAR_MODE

    bool showsProgressBar() const { return progressBarMode != HIDE_PROGRESS; }
    bool showsTitle() const { return titleMode != HIDE_TITLE; }
    bool showsClock() const { return clockMode != STATUS_BAR_CLOCK_HIDE; }
    // Visibility of the text lane. Clock hardware presence is the caller's
    // concern: pass halClock.isAvailable(), or true for layout reservation.
    bool textLaneVisible(bool clockAvailable) const {
      return showChapterPageCount || showBookProgressPercent || showsTitle() || showBattery ||
             (showsClock() && clockAvailable);
    }
  };
  StatusBarSpec statusBarSpec() const;

  // Resolved text-rendering configuration for the Epub layout engine. The
  // viewport is renderer/orientation-derived, so the caller supplies it -
  // passing it in keeps a spec from ever existing in a half-filled state.
  // Unlocked for the same reason as statusBarSpec(); see the note above.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
