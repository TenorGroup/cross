// The tenor/cross reading setup: thirteen settings a new card starts on, and that
// a settings file written before the setup existed is moved onto exactly once.
// Everything else in the file (language, remote, auto-sleep, button map, fonts)
// must come through untouched, and a user who changes a setup value afterwards
// must keep that change across the next load.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "SettingsList.h"

namespace settings_test_io {
extern int writes;
void setNextRead(const JsonDocument& doc);
}  // namespace settings_test_io

namespace {

struct Expected {
  const char* key;
  uint8_t value;
};

// The setup as the settings file stores it.
constexpr Expected TENOR[] = {
    {"extraParagraphSpacing", 1},
    {"lineSpacing", 2},
    {"wordSpacing", 3},
    {"paragraphIndent", 2},
    {"readerInkWeight", 1},
    {"frontButtonFollowOrientation", 1},
    {"shortPwrBtn", 3},
    {"sleepScreen", 10},
    {"statusBarClock", 1},
    {"tiltMenuNavigation", 1},
    {"tiltPageTurn", 1},
    {"tiltStrengthV", 0},
    {"tiltTabNavigation", 2},
};

bool isTilt(const char* key) { return std::strncmp(key, "tilt", 4) == 0; }

bool inSetup(const char* key) {
  for (const auto& e : TENOR)
    if (std::strcmp(e.key, key) == 0) return true;
  return false;
}

bool isStamp(const char* key) {
  return std::strcmp(key, "textSpacingVersion") == 0 || std::strcmp(key, "paragraphIndentVersion") == 0 ||
         std::strcmp(key, "readerInkWeightVersion") == 0 || std::strcmp(key, "tenorPresetVersion") == 0;
}

// A settings file as the release before the setup wrote it on a board with the
// motion sensor: every key present, all three older version stamps current.
constexpr const char* PREVIOUS_RELEASE_FILE = R"({
  "textSpacingVersion":3,"paragraphIndentVersion":1,"readerInkWeightVersion":1,"uiTheme":4,"uiTextSize":0,
  "globalStatusBarMode":0,"tenorSideArrows":1,"tenorButtonSymbols":1,"hideBatteryPercentage":0,
  "refreshFrequency":3,"fadingFix":0,"screenInverted":0,"sleepScreen":8,"sleepScreenCoverMode":0,
  "sleepScreenCoverFilter":0,"quickResumeSleepScreen":0,"wakeIntoBook":0,"sleepBwRefresh":1,"fontFamily":0,
  "lineSpacing":0,"letterSpacing":0,"wordSpacing":0,"extraParagraphSpacing":0,"paragraphAlignment":0,
  "screenMargin":5,"paragraphIndent":1,"embeddedStyle":1,"dropCapMode":1,"hyphenationEnabled":0,
  "orientation":0,"readerInkWeight":0,"textAntiAliasing":1,"imageRendering":0,"readerStatusBarMode":2,
  "sideButtonLayout":0,"tiltPageTurn":0,"tiltTabNavigation":0,"tiltMenuNavigation":0,"tiltStrengthH":1,
  "tiltStrengthV":1,"frontButtonFollowOrientation":0,"keyboardAligned":1,"keyboardAxisSwapped":1,
  "deviceName":"","longPressButtonBehavior":1,"longPressMenuFunction":1,"shortPwrBtn":0,
  "pwrBtnFootnoteBack":1,"sleepTimeoutMinutes":10,"showHiddenFiles":0,"removeReadBooksFromRecents":0,
  "moveFinishedToReadFolder":0,"opdsDownloadFolder":"","opdsFilenameFormat":0,"frontlightOn":0,
  "statusBarChapterPageCount":1,"statusBarBookProgressPercentage":1,"statusBarProgressBar":2,
  "statusBarProgressBarThickness":1,"statusBarTitle":1,"statusBarBattery":1,"xtcStatusBarMode":0,
  "statusBarClock":0,"clockAutoTimezone":1,"clockUtcOffsetQ":48,"clockFormat":0,"clockHasBeenSynced":0,
  "statusBarItemsMode":2,"frontButtonBack":0,"frontButtonConfirm":1,"frontButtonLeft":2,"frontButtonRight":3,
  "fontSize":16,"sdFontFamilyName":"","language":"EN","blePageTurnerEnabled":0,"blePeerAddr":"",
  "blePeerName":"","blePrevKeyUsage":0,"bleNextKeyUsage":0})";

bool expect(const bool condition, const char* label) {
  if (!condition) std::printf("FAIL %s\n", label);
  return condition;
}

bool setupValuesIn(const JsonDocument& saved, const bool hasImu, const char* label) {
  bool ok = true;
  for (const auto& e : TENOR) {
    if (isTilt(e.key) && !hasImu) {
      if (!saved[e.key].isNull()) {
        std::printf("FAIL %s: %s written on a board without the motion sensor\n", label, e.key);
        ok = false;
      }
      continue;
    }
    const int got = saved[e.key].isNull() ? -1 : saved[e.key].as<int>();
    if (got != e.value) {
      std::printf("FAIL %s: %s=%d, setup is %d\n", label, e.key, got, e.value);
      ok = false;
    }
  }
  return ok;
}

// Every key of `before` outside the setup comes out of `after` unchanged.
bool othersKept(const JsonDocument& before, const JsonDocument& after, const char* label) {
  bool ok = true;
  for (const JsonPairConst kv : before.as<JsonObjectConst>()) {
    const char* key = kv.key().c_str();
    if (inSetup(key) || isStamp(key)) continue;
    if (after[key] != kv.value()) {
      std::string was, now;
      serializeJson(kv.value(), was);
      serializeJson(after[key], now);
      std::printf("FAIL %s: %s was %s, now %s\n", label, key, was.c_str(), now.c_str());
      ok = false;
    }
  }
  return ok;
}

JsonDocument saved() {
  JsonDocument doc;
  SETTINGS.toJson(doc);
  return doc;
}

JsonDocument previousRelease() {
  JsonDocument doc;
  deserializeJson(doc, PREVIOUS_RELEASE_FILE);
  // Only an X3 writes this row, and the host build is not one.
  doc.remove("sleepBwRefresh");
  return doc;
}

// (a) A card with no settings file runs on what the object starts with.
int runBlankCard() {
  JsonDocument fresh = saved();
  bool ok = setupValuesIn(fresh, true, "blank card");
  ok = expect(fresh["language"] == "EN", "blank card language stays English") && ok;
  ok = expect(fresh["blePageTurnerEnabled"] == 0, "blank card Bluetooth page turner stays off") && ok;
  ok = expect(fresh["sleepTimeoutMinutes"] == 10, "blank card auto-sleep stays 10 minutes") && ok;
  ok = expect(fresh["frontButtonBack"] == 0 && fresh["frontButtonConfirm"] == 1 && fresh["frontButtonLeft"] == 2 &&
                  fresh["frontButtonRight"] == 3 && fresh["sideButtonLayout"] == 0,
              "blank card button map stays the stock one") &&
       ok;
  ok = expect(fresh["tenorPresetVersion"] == 1, "blank card is stamped, so its first save is never moved again") && ok;
  // Only the thirteen moved: every other default matches the previous release's file
  // (the font name there is what a card without the shipped font clears it to).
  JsonDocument before = previousRelease();
  before["sdFontFamilyName"] = SETTINGS.sdFontFamilyName;
  ok = othersKept(before, fresh, "blank card other defaults") && ok;
  std::printf("tenor_preset=blank:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// (b) A file from the previous release, with the owner's own choices, is moved once.
JsonDocument ownersFile() {
  JsonDocument doc = previousRelease();
  doc["sleepScreen"] = 3;
  doc["lineSpacing"] = 0;
  doc["language"] = "VI";
  doc["blePageTurnerEnabled"] = 1;
  doc["blePeerAddr"] = "0A:1B:2C:3D:4E:5F";
  doc["blePeerName"] = "Page remote";
  doc["sleepTimeoutMinutes"] = 31;
  doc["frontButtonBack"] = 1;
  doc["frontButtonConfirm"] = 0;
  doc["frontButtonLeft"] = 3;
  doc["frontButtonRight"] = 2;
  doc["sideButtonLayout"] = 1;
  doc["uiTextSize"] = 1;
  doc["refreshFrequency"] = 1;
  doc["screenMargin"] = 15;
  doc["fontSize"] = 18;
  doc["tiltStrengthH"] = 2;
  doc["deviceName"] = "reader-two";
  return doc;
}

int runOldFile() {
  halTiltSensor.available = true;
  const JsonDocument before = ownersFile();
  settings_test_io::setNextRead(before);
  bool ok = expect(SETTINGS.loadFromFile(), "previous release file loads");
  ok = expect(settings_test_io::writes == 1, "moving onto the setup saves the file once") && ok;
  const JsonDocument after = saved();
  ok = setupValuesIn(after, true, "previous release file") && ok;
  ok = othersKept(before, after, "previous release file") && ok;
  ok = expect(after["tenorPresetVersion"] == 1, "moved file carries the setup stamp") && ok;
  std::printf("tenor_preset=old-file:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// (c) After the move, a setup value the owner sets back survives the next load.
int runOnlyOnce() {
  halTiltSensor.available = true;
  settings_test_io::setNextRead(ownersFile());
  bool ok = expect(SETTINGS.loadFromFile(), "previous release file loads");
  SETTINGS.sleepScreen = CrossPointSettings::COVER;
  SETTINGS.lineSpacing = readerSpacing::LEVEL_DEFAULT;
  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_OFF;
  ok = expect(SETTINGS.saveToFile(), "owner's change saves") && ok;
  const int writesBefore = settings_test_io::writes;
  ok = expect(SETTINGS.loadFromFile(), "second load") && ok;
  ok = expect(settings_test_io::writes == writesBefore, "second load does not resave") && ok;
  ok = expect(SETTINGS.sleepScreen == CrossPointSettings::COVER &&
                  SETTINGS.lineSpacing == readerSpacing::LEVEL_DEFAULT &&
                  SETTINGS.tiltPageTurn == CrossPointSettings::TILT_OFF,
              "owner's changes survive the second load") &&
       ok;
  ok = expect(SETTINGS.wordSpacing == readerSpacing::WIDE && SETTINGS.shortPwrBtn == CrossPointSettings::FORCE_REFRESH,
              "setup values the owner left alone stay") &&
       ok;
  std::printf("tenor_preset=once:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// (d) The older version folds run first and never rewrite a setup value, on the
// move and on every load after it.
int runOldSpacing() {
  halTiltSensor.available = true;
  bool ok = true;
  for (const int spacingVersion : {-1, 1, 2}) {
    JsonDocument before = previousRelease();
    if (spacingVersion < 0) {
      before.remove("textSpacingVersion");
    } else {
      before["textSpacingVersion"] = spacingVersion;
    }
    before.remove("paragraphIndentVersion");
    before.remove("readerInkWeightVersion");
    before["lineSpacing"] = 1;
    before["letterSpacing"] = 0;
    before["extraParagraphSpacing"] = 2;
    before["paragraphIndent"] = 2;
    before["readerInkWeight"] = 3;
    settings_test_io::setNextRead(before);
    ok = expect(SETTINGS.loadFromFile(), "old spacing file loads") && ok;
    ok = setupValuesIn(saved(), true, "old spacing file, first load") && ok;
    ok = expect(SETTINGS.letterSpacing == readerSpacing::NARROW, "letter spacing still takes its old fold") && ok;
    const int writesBefore = settings_test_io::writes;
    ok = expect(SETTINGS.loadFromFile(), "old spacing file, second load") && ok;
    ok = setupValuesIn(saved(), true, "old spacing file, second load") && ok;
    ok = expect(settings_test_io::writes == writesBefore, "second load reads the stamps and does not resave") && ok;
  }
  std::printf("tenor_preset=old-spacing:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// Boards without the motion sensor carry the tilt fields but never write them.
int runNoImu() {
  halTiltSensor.available = false;
  JsonDocument before = ownersFile();
  for (const auto& e : TENOR)
    if (isTilt(e.key)) before.remove(e.key);
  before.remove("tiltStrengthH");
  settings_test_io::setNextRead(before);
  bool ok = expect(SETTINGS.loadFromFile(), "file without tilt keys loads");
  const JsonDocument after = saved();
  ok = setupValuesIn(after, false, "no motion sensor") && ok;
  ok = othersKept(before, after, "no motion sensor") && ok;
  std::printf("tenor_preset=no-imu:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode = argv[1];
  halTiltSensor.available = true;
  if (mode == "blank") return runBlankCard();
  if (mode == "old-file") return runOldFile();
  if (mode == "once") return runOnlyOnce();
  if (mode == "old-spacing") return runOldSpacing();
  if (mode == "no-imu") return runNoImu();
  return 2;
}
