// Upgrade round trip through the merged (CrossPoint 1.6.5) settings code.
//
// A real settings.json written by v1.0.18 or v1.0.19 must load and re-save with every
// stored value intact. This is the read side of the same contract test/tenor_preset
// already exercises for the write side (a fresh card's defaults, and the one-time Tenor
// preset move): here the input is an actual device file, not a hand-built one.
//
// Three real fixtures (test/settings_upgrade/fixtures/, personal fields replaced with
// synthetic ones of the same shape, every other value kept exactly as read off a device):
//   settings-v1.0.19.json  89 keys, current format
//   settings-v1.0.18.json  87 keys, missing screenTapAction/edgeTapAction (added in 1.0.19,
//                           unrelated to the 1.6.5 merge)
//   settings-cu-bak.json   69 keys, an older pre-1.0.18 format
//
// Every key present in an input file must come back with the same value after
// load+save, except three documented exceptions (see the arrays below):
//   - HOST_BOARD_HIDDEN: real device-only rows the host build's board selection hides
//     from toJson() regardless of input (test/tenor_preset already carries this same
//     caveat for sleepBwRefresh - "Only an X3 writes this row, and the host build is
//     not one").
//   - the 13-key Tenor preset move (applyTenorPreset(), tenorPresetVersion gate): this
//     predates the 1.6.5 merge (test/tenor_preset covers it on its own) and fires here
//     only because settings-cu-bak.json has no tenorPresetVersion stamp.
//   - keys with no mapping in the current schema at all (already gone before v1.0.18).
// None of this is caused by CrossPoint 1.6.5: settings-v1.0.19.json and
// settings-v1.0.18.json round-trip with ZERO exceptions beyond the host-board caveat.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "SettingsList.h"

namespace settings_test_io {
extern int writes;
void setNextRead(const JsonDocument& doc);
}  // namespace settings_test_io

namespace {

bool expect(const bool condition, const char* label) {
  if (!condition) std::printf("FAIL %s\n", label);
  return condition;
}

JsonDocument readFixture(const char* name) {
  std::ifstream in(std::string(FIXTURES) + "/" + name);
  std::stringstream buf;
  buf << in.rdbuf();
  JsonDocument doc;
  deserializeJson(doc, buf.str());
  return doc;
}

JsonDocument saved() {
  JsonDocument doc;
  SETTINGS.toJson(doc);
  return doc;
}

bool contains(const std::vector<std::string>& keys, const char* key) {
  for (const auto& k : keys)
    if (k == key) return true;
  return false;
}

// A real device row the host build's board selection never writes (host build defines
// both FREEINK_DEVICE_X3 and FREEINK_DEVICE_X4; BoardConfig::ACTIVE resolves to neither
// exactly, and FREEINK_CAP_FRONTLIGHT/WARMLIGHT=0 mean no frontlight either). This is a
// host-test-harness artifact, not a real round-trip loss - test/tenor_preset documents
// the same thing for sleepBwRefresh ("Only an X3 writes this row, and the host build is
// not one").
const std::vector<std::string> HOST_BOARD_HIDDEN = {"sleepBwRefresh", "frontlightBrightness",
                                                     "frontlightWarmth"};

// Every key present in `before` must equal the same key in `after`, except keys in
// `skip` (checked separately, with their expected new meaning spelled out at the
// call site). Reports every mismatch instead of stopping at the first.
bool roundTripsExcept(const JsonDocument& before, const JsonDocument& after,
                     const std::vector<std::string>& skip, const char* label) {
  bool ok = true;
  for (const JsonPairConst kv : before.as<JsonObjectConst>()) {
    const char* key = kv.key().c_str();
    if (contains(skip, key)) continue;
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

// (a) and (b): a real v1.0.19 file and the v1.0.18 file it evolved from (same keys minus
// screenTapAction/edgeTapAction, added in 1.0.19 - unrelated to this merge). Neither file
// crosses any migration this merge introduced: every stored key keeps its value, and the
// two clock keys the merge (#3562) actually touches behave exactly as the coordinator's
// merge-resolution note said they would.
int runRealFile(const char* fixtureName, const char* label) {
  const JsonDocument before = readFixture(fixtureName);
  settings_test_io::setNextRead(before);
  bool ok = expect(SETTINGS.loadFromFile(), "fixture loads");
  const JsonDocument after = saved();
  ok = roundTripsExcept(before, after, HOST_BOARD_HIDDEN, label) && ok;

  // clockUtcOffsetQ (legacy quarter-hour offset) stays a stored key: #3562 added
  // clockTimezone/clockDst/clockShowInHeader beside it, it did not retype or drop it.
  ok = expect(SETTINGS.clockUtcOffsetQ == 76, "clockUtcOffsetQ still reads 76") && ok;
  // clockTimezone is new (#3562) and this file predates it: unset (255), falling back
  // to the legacy offset above, exactly as the merge resolution documents.
  ok = expect(SETTINGS.clockTimezone == 255, "clockTimezone stays unset (255)") && ok;
  ok = expect(SETTINGS.clockDst == CrossPointSettings::CLOCK_DST_AUTO, "clockDst takes its new default") && ok;
  ok = expect(SETTINGS.clockShowInHeader == 0, "clockShowInHeader takes its new default") && ok;

  // Second load of what was just saved must not drift further (no double migration).
  ok = expect(SETTINGS.saveToFile(), "save succeeds") && ok;
  ok = expect(SETTINGS.loadFromFile(), "second load") && ok;
  ok = roundTripsExcept(before, saved(), HOST_BOARD_HIDDEN, "second load") && ok;

  std::printf("settings_upgrade=%s:%s\n", label, ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// (c) An older, pre-1.0.18 file. It predates three things unrelated to the 1.6.5 merge:
//   - the Tenor preset stamp (tenorPresetVersion absent -> applyTenorPreset() fires,
//     forcing 13 keys to the shipped defaults regardless of what the file held -
//     test/tenor_preset::runOldFile exercises this exact path on its own);
//   - dropCapMode (absent -> its predecessor focusReadingEnabled supplies the value);
//   - a handful of keys with no mapping in the schema at all (dashboard*, the old
//     three-way back/quick-action bindings, uiScale, textDarkness, ...): already gone
//     before v1.0.18, not something this merge touched.
// Every other key in the file must round-trip unchanged, same as the two real files above.
int runOlderFile() {
  const JsonDocument before = readFixture("settings-cu-bak.json");
  settings_test_io::setNextRead(before);
  bool ok = expect(SETTINGS.loadFromFile(), "older file loads");
  const int writesAfterLoad = settings_test_io::writes;
  ok = expect(writesAfterLoad == 1, "older file resaves once (preset move + folds)") && ok;

  // The 13-key Tenor preset move: pre-existing (predates this merge), forces these
  // regardless of what the file held. Values match CrossPointSettings::applyTenorPreset()
  // and test/tenor_preset's own TENOR[] table.
  struct { const char* key; int forced; } PRESET[] = {
      {"extraParagraphSpacing", 1}, {"lineSpacing", 2},  {"wordSpacing", 3},
      {"paragraphIndent", 2},      {"readerInkWeight", 1}, {"frontButtonFollowOrientation", 1},
      {"shortPwrBtn", 3},          {"sleepScreen", 10},   {"statusBarClock", 1},
      {"tiltMenuNavigation", 1},   {"tiltPageTurn", 1},   {"tiltStrengthV", 0},
      {"tiltTabNavigation", 2},
  };
  std::vector<std::string> skip = HOST_BOARD_HIDDEN;
  for (const auto& p : PRESET) skip.push_back(p.key);

  // focusReadingEnabled (old boolean) migrates into dropCapMode (new 3-value enum): the
  // key itself has no successor of the same name, so it is excluded from the generic
  // sweep and checked here with the new key's expected value instead.
  skip.push_back("focusReadingEnabled");

  // Keys with no mapping in the current schema at all (verified empirically: absent
  // from toJson() output, and confirmed unrelated to this merge by re-running this same
  // fixture against the pre-merge commit, see NOTES.md).
  const char* DROPPED[] = {
      "dashboardClockFormat", "dashboardMenuLayout",  "dashboardShowDate",  "dashboardShowStatus",
      "dashboardShowStreak",  "dashboardStatus",      "longPressMenuFunctions",
      "readerLongBackAction", "readerQuickActionBinding", "readerShortBackAction",
      "readingIdleTimeThresholdUnits", "textDarkness", "tiltPageTurnLastEnabled",
      "trackReadingStats",    "uiScale",
  };
  for (const char* k : DROPPED) skip.push_back(k);

  const JsonDocument after = saved();
  ok = roundTripsExcept(before, after, skip, "older file") && ok;

  // Not skipped, and worth calling out: backShortToFileBrowser=0 now round-trips (it did
  // NOT before this merge - a pre-existing Tenor bug, a v.push_back() missing from a prior
  // refactor, silently dropped the row from the catalog; the merge resolution restored it
  // while reconciling SettingsList.h against upstream's copy of the same file). Confirmed
  // by running this exact fixture through the pre-merge commit (14b8e413): there, this one
  // key showed the same "before=0 after=null" drop as the DROPPED keys above; here it does
  // not, because roundTripsExcept() above already asserts it equals its input value.

  for (const auto& p : PRESET) {
    if (before[p.key].isNull()) continue;  // not every preset key is present in this file
    const int got = after[p.key].isNull() ? -1 : after[p.key].as<int>();
    if (got != p.forced) {
      std::printf("FAIL older file: %s forced to %d by the Tenor preset move, got %d\n", p.key, p.forced, got);
      ok = false;
    }
  }
  ok = expect(SETTINGS.dropCapMode == 0 /* DROP_CAP_OFF */,
             "focusReadingEnabled=0 migrates to dropCapMode=DROP_CAP_OFF") && ok;

  ok = expect(SETTINGS.clockUtcOffsetQ == 76, "clockUtcOffsetQ still reads 76") && ok;
  ok = expect(SETTINGS.clockTimezone == 255, "clockTimezone stays unset (255)") && ok;

  // A second load of the resaved file must not migrate again (the preset stamp now
  // matches, and no other fold's condition can retrigger).
  ok = expect(SETTINGS.loadFromFile(), "second load") && ok;
  ok = expect(settings_test_io::writes == writesAfterLoad, "second load does not resave") && ok;

  std::printf("settings_upgrade=older:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// A synthetic addendum, not one of the three real files: none of them carry
// "touchReaderControls" (it is touch-only, and every real fixture here is an X3), so the
// one true migration this merge (#3586) introduces never fires on real Tenor hardware
// data. This case proves the migration itself still works, starting from the real
// v1.0.19 fixture with that one key added by hand, as an old touch-board file would have
// had it. touchReaderControls/pageTurnGesture/previousPageGesture are touch-only rows
// (settingHiddenOnThisBoard hides them from toJson() on X3/X4), so this checks the
// struct fields fromJson() sets directly rather than the saved JSON.
int runTouchMigration() {
  JsonDocument before = readFixture("settings-v1.0.19.json");
  before["touchReaderControls"] = 2;  // legacy: 0 off / 1 tap / 2 swipe / 3 inverted tap
  settings_test_io::setNextRead(before);
  bool ok = expect(SETTINGS.loadFromFile(), "synthetic old-touch file loads");
  ok = expect(SETTINGS.touchReaderControls == CrossPointSettings::TOUCH_READER_ON,
             "touchReaderControls folds to the new master on/off") && ok;
  ok = expect(SETTINGS.pageTurnGesture == CrossPointSettings::SWIPE_ONLY,
             "legacy mode 2 (swipe) becomes pageTurnGesture=SWIPE_ONLY") && ok;
  ok = expect(SETTINGS.previousPageGesture == CrossPointSettings::SWIPE_ONLY,
             "previousPageGesture takes the same gesture on first fold") && ok;
  std::printf("settings_upgrade=touch-migration:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode = argv[1];
  halTiltSensor.available = true;
  if (mode == "v1019") return runRealFile("settings-v1.0.19.json", "v1.0.19");
  if (mode == "v1018") return runRealFile("settings-v1.0.18.json", "v1.0.18");
  if (mode == "older") return runOlderFile();
  if (mode == "touch-migration") return runTouchMigration();
  return 2;
}
