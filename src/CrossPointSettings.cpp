#include "CrossPointSettings.h"

#include <Epub/ReaderSpacing.h>
#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "SettingsList.h"
#include "activities/reader/ReaderMenuLayout.h"
#include "fontIds.h"

namespace {

// Stack buffer for "<key>_obf" key construction - avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

// Tran cua danh sach ghim phai khop giua noi LUU va noi DUNG. Lech thi mot muc ghim
// bien mat im lang sau khi tat may.
static_assert(CrossPointSettings::READER_FAVORITE_MAX == readermenu::TOI_DA_GHIM,
              "tran danh sach yeu thich lech giua CrossPointSettings va readermenu");

void CrossPointSettings::toJson(JsonDocument& doc) const {
  doc["textSpacingVersion"] = 3;
  doc["paragraphIndentVersion"] = 1;
  const CrossPointSettings& s = *this;

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files - skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  // Front button remap - managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Font family and size - both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["fontSize"] = fontPointSize;
  // SD card font family name - not in SettingsList, save manually
  doc["sdFontFamilyName"] = sdFontFamilyName;
  // Dictionary folder name - uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";

  // A uint16_t mask, so it does not fit the uint8_t generic loop. Omitted while
  // unconfigured, so the default keeps following the UI language.
  if (keyboardLayouts != 0) {
    doc["keyboardLayouts"] = keyboardLayouts;
  }

  // Tab Yeu thich: mot DANH SACH co thu tu, nen vong lap uint8_t chung khong cha duoc.
  // Bo han khoa khi nguoi doc chua tung ghim gi, de ban mac dinh con duong doi ve sau.
  if (readerFavoritesDaDat) {
    JsonArray yeuThich = doc["readerFavorites"].to<JsonArray>();
    for (uint8_t i = 0; i < readerFavoriteCount && i < READER_FAVORITE_MAX; i++) {
      yeuThich.add(readerFavorites[i]);
    }
  }

  // BLE page turner (BTH2) - managed by BlePageTurnerActivity, not SettingsList.
  doc["blePageTurnerEnabled"] = blePageTurnerEnabled;
  doc["blePeerAddr"] = blePeerAddr;
  doc["blePeerName"] = blePeerName;
  doc["blePrevKeyUsage"] = blePrevKeyUsage;
  doc["bleNextKeyUsage"] = bleNextKeyUsage;
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files - skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        continue;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  // v1.0.2 luu hai co an rieng cho thanh trang thai ngoai/trong trinh doc. Quy doi
  // theo dung y dinh cu, nhung CHI khi file chua co khoa moi, de mot file v3 khong
  // bi ghi de boi co cu.
  if (doc["globalStatusBarMode"].isNull() && !doc["hideGlobalStatusBar"].isNull()) {
    globalStatusBarMode =
        (doc["hideGlobalStatusBar"] | uint8_t{0}) ? GLOBAL_STATUS_BAR_OFF : GLOBAL_STATUS_BAR_SMALL;
    needsResave = true;
  }
  if (doc["readerStatusBarMode"].isNull() && !doc["hideReaderStatusBar"].isNull()) {
    readerStatusBarMode =
        (doc["hideReaderStatusBar"] | uint8_t{0}) ? READER_STATUS_BAR_OFF : READER_STATUS_BAR_DEFAULT;
    needsResave = true;
  }

  // Retire the experimental hold-to-resize shortcut without delaying page turns.
  if ((doc["longPressButtonBehavior"] | uint8_t{OFF}) == FONT_SIZE_STEP) {
    longPressButtonBehavior = OFF;
    needsResave = true;
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // Text spacing levels (textSpacingVersion 3). The generic loop above already
  // took each stored ordinal through the five-value range check, so the fields
  // hold either a valid level or the row's default. The rules below read the
  // document as well, because an ABSENT key means "never set" and must keep the
  // new default instead of being folded as if it held an old ordinal, and
  // because an out-of-range value has to ask for a resave rather than silently
  // become a level.
  {
    const uint8_t spacingVersion = doc["textSpacingVersion"] | 0;
    if (spacingVersion < 3) {
      // Line and letter shared one three-value row; paragraph spacing had its
      // own. Anything outside the old range is a repair, and every file below
      // v3 is rewritten once so the version stamp sticks.
      if (!doc["lineSpacing"].isNull()) {
        const uint8_t raw = lineSpacing == 3 ? 2 : lineSpacing;  // v1/v2 treated a stored 3 as Wide
        lineSpacing = raw > 2 ? readerSpacing::LEVEL_DEFAULT : readerSpacing::legacyLineLetterLevel(raw);
      }
      if (!doc["letterSpacing"].isNull()) {
        letterSpacing = letterSpacing > 2 ? readerSpacing::LEVEL_DEFAULT
                                         : readerSpacing::legacyLineLetterLevel(letterSpacing);
      }
      // wordSpacing did not exist before v3, so an old file has either no key
      // (keep the default) or a hand-edit the five-value row cannot hold.
      if (!doc["wordSpacing"].isNull() && doc["wordSpacing"].as<uint8_t>() >= readerSpacing::LEVEL_COUNT) {
        wordSpacing = readerSpacing::LEVEL_DEFAULT;
      }
      if (!doc["extraParagraphSpacing"].isNull()) {
        // Files older than v2 stored a toggle-like value that was already folded
        // once, by the v2 release; fold it to the v2 meaning first, exactly as
        // that release did, then to a level.
        uint8_t v2 = extraParagraphSpacing;
        if (spacingVersion < 2) {
          v2 = doc["textSpacingVersion"].isNull() ? (v2 != 0 ? 1 : 0) : (v2 == 2 ? 1 : 0);
        }
        extraParagraphSpacing =
            v2 > 2 ? readerSpacing::LEVEL_DEFAULT : readerSpacing::legacyParagraphLevel(v2);
      }
      needsResave = true;
    } else {
      // Already v3: these ordinals never re-enter the folds. Only a value the
      // row cannot hold is repaired, and that is worth a resave.
      const auto repairLevel = [&doc, &needsResave](const char* key, uint8_t& field) {
        if (doc[key].isNull()) return;  // absent keeps the default level
        const uint8_t raw = doc[key].as<uint8_t>();
        if (raw < readerSpacing::LEVEL_COUNT) {
          field = raw;
        } else {
          field = readerSpacing::LEVEL_DEFAULT;
          needsResave = true;
        }
      };
      repairLevel("lineSpacing", lineSpacing);
      repairLevel("letterSpacing", letterSpacing);
      repairLevel("wordSpacing", wordSpacing);
      repairLevel("extraParagraphSpacing", extraParagraphSpacing);
    }
  }
  if (doc["paragraphIndentVersion"].isNull() && !doc["paragraphIndent"].isNull()) {
    paragraphIndent = doc["paragraphIndent"].as<uint8_t>() == 2 ? 0 : 1;
    needsResave = true;
  }
  // Drop cap: the old boolean became a three-value mode. A file that had the
  // feature on keeps the size it used to draw (Large); off becomes Off; a file
  // with neither key keeps the new smaller default. An out-of-range mode is
  // repaired and the file is resaved once.
  if (doc["dropCapMode"].isNull()) {
    if (!doc["focusReadingEnabled"].isNull()) {
      dropCapMode = (doc["focusReadingEnabled"].as<uint8_t>() != 0) ? readerSpacing::DROP_CAP_LARGE
                                                                   : readerSpacing::DROP_CAP_OFF;
      needsResave = true;
    }
  } else {
    const uint8_t raw = doc["dropCapMode"].as<uint8_t>();
    if (raw < readerSpacing::DROP_CAP_MODE_COUNT) {
      dropCapMode = raw;
    } else {
      dropCapMode = readerSpacing::DROP_CAP_DEFAULT;
      needsResave = true;
    }
  }
  // Front button remap - managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  // Reader font size - an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  uint8_t storedFontSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;
  if (storedFontSize <= LEGACY_FONT_SIZE_MAX) {
    storedFontSize = 12 + storedFontSize * 2;  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }
  fontPointSize = storedFontSize;

  // Font family - uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // SD card font family name - not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = NOTOSERIF;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  // Dictionary folder name - uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));

  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  // Absent means unconfigured, which is the default.
  if (doc["keyboardLayouts"].is<uint16_t>()) {
    keyboardLayouts = doc["keyboardLayouts"].as<uint16_t>();
  }

  // Tab Yeu thich. Vang mat nghia la nguoi doc chua tung ghim gi, va luc do man menu se
  // dung ban mac dinh cua no; o day de nguyen readerFavoriteCount bang 0.
  //
  // Loc o vuot tam ngay luc nap: mot ban ghi cu co the mang so muc khong con ton tai,
  // va mot o rac ma lot vao la man menu tro toi mot lenh khong co that.
  if (doc["readerFavorites"].is<JsonArrayConst>()) {
    readerFavoritesDaDat = 1;
    readerFavoriteCount = 0;
    for (const JsonVariantConst o : doc["readerFavorites"].as<JsonArrayConst>()) {
      if (readerFavoriteCount >= READER_FAVORITE_MAX) break;
      if (!o.is<uint8_t>()) continue;
      const uint8_t v = o.as<uint8_t>();
      if (v >= static_cast<uint8_t>(readermenu::ACTION_COUNT)) {
        needsResave = true;
        continue;
      }
      readerFavorites[readerFavoriteCount++] = v;
    }
  }

  // BLE page turner (BTH2). Khong ban phat hanh nao truoc day ghi nam khoa nay,
  // nen mot file v1.0.2 thieu chung nghia la "chua ai dung": giu dung mac dinh
  // trong struct (tat, khong peer, chua hoc nut) va ghi lai file MOT lan de nam
  // khoa co mat tu day. Cac truong cu di qua vong lap chung o tren nen khong
  // truong nao bi mat.
  blePageTurnerEnabled = (doc["blePageTurnerEnabled"] | uint8_t{0}) ? 1 : 0;
  copyToField(blePeerAddr, doc["blePeerAddr"] | "", sizeof(blePeerAddr));
  copyToField(blePeerName, doc["blePeerName"] | "", sizeof(blePeerName));
  blePrevKeyUsage = doc["blePrevKeyUsage"] | uint8_t{0};
  bleNextKeyUsage = doc["bleNextKeyUsage"] | uint8_t{0};
  if (doc["blePageTurnerEnabled"].isNull()) {
    needsResave = true;
  }

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  if (readerStatusBarHidden()) return spec;
  // Sau muc nguoi dung chon. Moi muc chi bat dung cac thanh phan co ten trong muc,
  // va ca sau muc deu nam trong CUNG mot lan chu (tru Tat) nen doi muc khong lam
  // doi chieu cao trang: chi ve lai thanh trang thai, khong dan lai sach.
  switch (readerStatusBarMode) {
    case READER_STATUS_BAR_CLOCK_BATTERY:
      spec.showChapterPageCount = false;
      spec.showBookProgressPercent = false;
      spec.titleMode = HIDE_TITLE;
      spec.showBattery = true;
      spec.showBatteryPercent = true;
      spec.clockMode = statusBarClock == STATUS_BAR_CLOCK_LEFT ? STATUS_BAR_CLOCK_LEFT : STATUS_BAR_CLOCK_RIGHT;
      spec.progressBarMode = HIDE_PROGRESS;
      spec.progressBarHeightPx = 0;
      spec.xtcMode = XTC_STATUS_BAR_BOTTOM;
      break;
    case READER_STATUS_BAR_CHAPTER_PROGRESS:
      spec.showChapterPageCount = true;
      spec.showBookProgressPercent = false;
      spec.titleMode = CHAPTER_TITLE;
      spec.showBattery = false;
      spec.showBatteryPercent = false;
      spec.clockMode = STATUS_BAR_CLOCK_HIDE;
      spec.progressBarMode = HIDE_PROGRESS;
      spec.progressBarHeightPx = 0;
      spec.xtcMode = XTC_STATUS_BAR_BOTTOM;
      break;
    case READER_STATUS_BAR_CHAPTER_CLOCK:
      spec.showChapterPageCount = false;
      spec.showBookProgressPercent = false;
      spec.titleMode = CHAPTER_TITLE;
      spec.showBattery = false;
      spec.showBatteryPercent = false;
      spec.clockMode = statusBarClock == STATUS_BAR_CLOCK_LEFT ? STATUS_BAR_CLOCK_LEFT : STATUS_BAR_CLOCK_RIGHT;
      spec.progressBarMode = HIDE_PROGRESS;
      spec.progressBarHeightPx = 0;
      spec.xtcMode = XTC_STATUS_BAR_BOTTOM;
      break;
    case READER_STATUS_BAR_CHAPTER_BATTERY:
      spec.showChapterPageCount = false;
      spec.showBookProgressPercent = false;
      spec.titleMode = CHAPTER_TITLE;
      spec.showBattery = true;
      spec.showBatteryPercent = true;
      spec.clockMode = STATUS_BAR_CLOCK_HIDE;
      spec.progressBarMode = HIDE_PROGRESS;
      spec.progressBarHeightPx = 0;
      spec.xtcMode = XTC_STATUS_BAR_BOTTOM;
      break;
    case READER_STATUS_BAR_DEFAULT:
    default:
      spec.showChapterPageCount = true;
      spec.showBookProgressPercent = true;
      spec.titleMode = CHAPTER_TITLE;
      spec.showBattery = true;
      spec.showBatteryPercent = true;
      spec.clockMode = statusBarClock == STATUS_BAR_CLOCK_LEFT ? STATUS_BAR_CLOCK_LEFT : STATUS_BAR_CLOCK_RIGHT;
      spec.progressBarMode = HIDE_PROGRESS;
      spec.progressBarHeightPx = 0;
      spec.xtcMode = XTC_STATUS_BAR_BOTTOM;
      break;
  }
  spec.clock12h = clockFormat == 1;
  spec.clockUtcOffsetQ = clockUtcOffsetQ;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing;
  spec.paragraphIndent = paragraphIndent;
  spec.letterSpacing = readerSpacing::letterPixels(letterSpacing);
  spec.wordSpacing = wordSpacing;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.dropCapMode = dropCapMode;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  // Mặc định has to mean the family's own default. The per-family tables this
  // used to carry were SD and Noto Serif at 0.95/1.00/1.10 and Noto Sans at
  // 0.90/0.95/1.00; with one five-level table the base is now expressed once and
  // each level adds a fixed step, so a Noto Sans reader keeps the 0.95 default
  // they already had and the other four levels move evenly around it.
  const float base = (sdFontFamilyName[0] == '\0' && fontFamily == NOTOSANS) ? 0.95f : 1.00f;
  return base + readerSpacing::lineFactorOffset(lineSpacing);
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_NEVER:
      // Effectively disables the periodic full refresh; the page counter
      // counts down from here and never reaches the threshold in practice.
      return std::numeric_limits<int>::max();
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating - this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  const bool sans = (fontFamily == NOTOSANS);
  switch (pt) {
    case 12:
      return sans ? NOTOSANS_12_FONT_ID : NOTOSERIF_12_FONT_ID;
    case 16:
      return sans ? NOTOSANS_16_FONT_ID : NOTOSERIF_16_FONT_ID;
    case 18:
      return sans ? NOTOSANS_18_FONT_ID : NOTOSERIF_18_FONT_ID;
    case 14:
    default:
      return sans ? NOTOSANS_14_FONT_ID : NOTOSERIF_14_FONT_ID;
  }
}
