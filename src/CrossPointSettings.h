#pragma once
#include <ArduinoJson.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/ReaderSpacing.h>
#include <PersistableStore.h>

#include <cstdint>

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

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
  // Default: Up = Previous, Down = Next
  // NEXT_NEXT is appended rather than slotted in beside its siblings: the value is
  // persisted as a number, so inserting would silently change what an existing
  // save means.
  enum SIDE_BUTTON_LAYOUT {
    PREV_NEXT = 0,
    NEXT_PREV = 1,
    SIDE_BUTTONS_DISABLED = 2,
    NEXT_NEXT = 3,
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

  // Short power button press actions
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    FOOTNOTES = 4,
    PWR_CONFIRM = 5,
    SHORT_PWRBTN_COUNT
  };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by index: any new function (e.g. dictionary, bookmark) MUST use a
  // value >= 2 and be appended at the END of the enumValues array in SettingsList.h, otherwise the
  // stored indices shift and existing saves are silently misinterpreted.
  enum LONG_PRESS_MENU_FUNCTION {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_READER_MENU = 4,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long press behavior
  // Mặc định theo quyết định founder v1.0.3: giữ nút bên cạnh = nhảy chương.
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,
    ORIENTATION_CHANGE = 2,
    FONT_SIZE_STEP = 3,  // giu nut tien = co chu to mot nac, giu nut lui = nho mot nac (14/09/2026)
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  // UI Theme
  enum UI_THEME { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3, TENOR_UI = 4 };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // How Select opens the reader menu: the classic full-screen list, or a toolbar
  // overlay (top/bottom bars with Contents / Text / More bottom-sheet panels)
  // painted over the page.
  enum READER_MENU_STYLE { READER_MENU_LIST = 0, READER_MENU_TOOLBAR = 1, READER_MENU_STYLE_COUNT };

  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_NVERTED = 2, TILT_PAGE_TURN_COUNT };

  enum TOUCH_READER_CONTROLS {
    TOUCH_READER_OFF = 0,
    TOUCH_READER_ON = 1,
    TOUCH_READER_SWIPE = 2,
    TOUCH_READER_INVERTED_TAP = 3,
    TOUCH_READER_CONTROLS_COUNT
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
  // Thanh trang thai ngoai trinh doc: dung ba muc founder chot. Gia tri luu xuong
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
  uint8_t statusBarChapterPageCount = 1;
  uint8_t statusBarBookProgressPercentage = 1;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  uint8_t statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  uint8_t statusBarTitle = CHAPTER_TITLE;
  uint8_t statusBarBattery = 1;
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  uint8_t statusBarClock = STATUS_BAR_CLOCK_HIDE;
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  // Quarter-hour granularity supports oddball zones like Nepal (+5:45) and Chatham (+12:45).
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  uint8_t clockAutoTimezone = 1;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings. Each of these three is a readerSpacing::Level, not
  // a private enum: the ordinal is what settings.json stores, and ReaderSpacing.h
  // turns it into the line factor, the letter delta and the paragraph gap.
  // toJson stamps textSpacingVersion 3; fromJson folds v1/v2 files into it.
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
  uint8_t wakeButtons = 0;  // X3: power, power/right side, power/both sides, all.
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
  // UI Theme
  uint8_t uiTheme = TENOR_UI;
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
  // Touch screen reader zones/gestures on boards with a touch controller.
  uint8_t touchReaderControls = TOUCH_READER_SWIPE;
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

  // --- BLE page turner (BTH2) -------------------------------------------------
  // Mac dinh TAT. Nam truong nay duoc luu tay trong toJson/fromJson (khong qua
  // SettingsList) vi man hinh cua chung la BlePageTurnerActivity, khong phai
  // man Cai dat chung. File v1.0.2 khong co nam khoa nay: doc len ra dung mac
  // dinh duoi day, va khong truong cu nao bi mat.
  //
  // `blePeerAddr`/`blePeerName` la thiet bi nguoi dung da chon: giu lai de lan
  // sau ket noi lai ma khong phai quet lai. `blePrevKeyUsage`/`bleNextKeyUsage`
  // la ma HID usage THO da hoc duoc tu report da phan giai; 0 = chua hoc.
  uint8_t blePageTurnerEnabled = 0;
  char blePeerAddr[18] = "";
  char blePeerName[32] = "";
  uint8_t blePrevKeyUsage = 0;
  uint8_t bleNextKeyUsage = 0;

  static constexpr uint8_t BLE_USAGE_NONE = 0;
  // HID Usage Tables, Keyboard/Keypad (page 0x07): Left 0x50, Right 0x4F,
  // Page Up 0x4B, Page Down 0x4E.
  static constexpr uint8_t BLE_USAGE_LEFT = 0x50;
  static constexpr uint8_t BLE_USAGE_RIGHT = 0x4F;
  static constexpr uint8_t BLE_USAGE_PAGE_UP = 0x4B;
  static constexpr uint8_t BLE_USAGE_PAGE_DOWN = 0x4E;

  enum class BlePageAction : uint8_t { None = 0, PreviousPage, NextPage };

  // Y nghia lat trang cua mot usage HID da phan giai. Nut DA HOC thay cho mac
  // dinh cua dung chieu do: hoc roi thi nut cu khong con lat trang nua, neu
  // khong nguoi dung khong bao gio bo duoc mot anh xa sai. Chua hoc gi thi chi
  // bon nut mac dinh duoi day duoc nhan - van ban go binh thuong KHONG lat
  // trang. Mot usage di kem modifier (Ctrl/Alt/...) khong tinh.
  BlePageAction blePageActionFor(const uint8_t usageId, const uint8_t mods) const {
    if (!blePageTurnerEnabled || usageId == BLE_USAGE_NONE || mods != 0) return BlePageAction::None;
    if (blePrevKeyUsage != BLE_USAGE_NONE && usageId == blePrevKeyUsage) return BlePageAction::PreviousPage;
    if (bleNextKeyUsage != BLE_USAGE_NONE && usageId == bleNextKeyUsage) return BlePageAction::NextPage;
    if (blePrevKeyUsage == BLE_USAGE_NONE && (usageId == BLE_USAGE_LEFT || usageId == BLE_USAGE_PAGE_UP)) {
      return BlePageAction::PreviousPage;
    }
    if (bleNextKeyUsage == BLE_USAGE_NONE && (usageId == BLE_USAGE_RIGHT || usageId == BLE_USAGE_PAGE_DOWN)) {
      return BlePageAction::NextPage;
    }
    return BlePageAction::None;
  }

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

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
    uint8_t clockUtcOffsetQ = 48;             // 48 = UTC+0
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
