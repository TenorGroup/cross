// The settings page saves every changed value in one POST. The reader status bar mode and
// the three switches can arrive together: the switches sent with a mode belong to that
// mode, as picking the mode on the reader and then flipping a switch would leave them.
// CrossPointWebServer::handlePostSettings() is compiled verbatim (CMake copies it into
// handle_post_settings.inc) against a fake request.

#include <Logging.h>

#include <cstdio>
#include <string>

#include "SettingsList.h"

namespace {
using S = CrossPointSettings;

struct FakeRequest {
  std::string body;
  int status = 0;
  bool hasArg(const char*) const { return true; }
  String arg(const char*) const { return String(body); }
  void send(int code, const char*, const String&) { status = code; }
};

struct FontSystem {
  SdCardFontRegistry fonts;
  const SdCardFontRegistry& registry() const { return fonts; }
} sdFontSystem;

inline const char* trWeb(Language language, StrId id) { return I18n::getInstance().get(id, language); }
DeserializationError deserializeJson(JsonDocument& doc, const String& text) {
  return ArduinoJson::deserializeJson(doc, text.c_str());
}
}  // namespace

// The clock zone is pushed into the clock by timezones::applyToClock(); the fake counts the calls.
namespace timezones {
int applied = 0;
inline void applyToClock() { ++applied; }
}  // namespace timezones

struct CrossPointWebServer {
  FakeRequest* server = nullptr;
  Language requestLanguage() const { return Language::EN; }
  bool applyUiTextSizeSetting(uint8_t) { return true; }
  void handlePostSettings();
};

#include "handle_post_settings.inc"

namespace {
int failures = 0;

void expect(const bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::printf("FAIL %s\n", what);
}

// A card whose switches belong to Chapter name & clock.
void loadCard() {
  JsonDocument doc;
  doc["readerStatusBarMode"] = S::READER_STATUS_BAR_CHAPTER_CLOCK;
  doc["statusBarItemsMode"] = S::READER_STATUS_BAR_CHAPTER_CLOCK;
  doc["statusBarTitle"] = S::CHAPTER_TITLE;
  doc["statusBarChapterPageCount"] = 0;
  doc["statusBarBookProgressPercentage"] = 0;
  SETTINGS.fromJson(doc.as<JsonVariantConst>());
}

int post(const std::string& body) {
  FakeRequest request{body};
  CrossPointWebServer web;
  web.server = &request;
  web.handlePostSettings();
  return request.status;
}

struct Items {
  bool title, pages, percent;
};

bool shows(const Items& want) {
  const auto spec = SETTINGS.statusBarSpec();
  return spec.showsTitle() == want.title && spec.showChapterPageCount == want.pages &&
         spec.showBookProgressPercent == want.percent;
}

// What the next boot and the next page load read back.
bool reloadShows(const Items& want) {
  JsonDocument saved;
  SETTINGS.toJson(saved);
  SETTINGS.fromJson(saved.as<JsonVariantConst>());
  SETTINGS.adoptReaderStatusItems();
  return shows(want);
}

void modeAndSwitchTogether() {
  loadCard();
  expect(post("{\"readerStatusBarMode\":2,\"statusBarTitle\":2}") == 200, "mode and switch saved");
  expect(SETTINGS.readerStatusBarMode == S::READER_STATUS_BAR_DEFAULT, "mode applied");
  expect(shows({false, true, true}), "the switch sent with the mode holds, the rest are the mode's");
  expect(reloadShows({false, true, true}), "the switch sent with the mode survives a reload");
}

void switchAlone() {
  loadCard();
  expect(post("{\"statusBarBookProgressPercentage\":1}") == 200, "switch saved");
  expect(shows({true, false, true}), "a switch alone lands on the current mode");
  expect(reloadShows({true, false, true}), "a switch alone survives a reload");
}

void modeAlone() {
  loadCard();
  expect(post("{\"readerStatusBarMode\":2}") == 200, "mode saved");
  expect(shows({true, true, true}), "a mode alone shows what its name says");
  expect(reloadShows({true, true, true}), "a mode alone survives a reload");
}

void zoneReachesTheClock() {
  loadCard();
  SETTINGS.clockAutoTimezone = 1;
  SETTINGS.clockDst = 0;
  timezones::applied = 0;
  expect(post("{\"clockAutoTimezone\":0}") == 200, "auto timezone off saved");
  expect(timezones::applied == 1, "turning Auto timezone off pushes the zone into the clock");
  expect(post("{\"clockDst\":1}") == 200, "dst saved");
  expect(timezones::applied == 2, "a new Dst pushes the zone into the clock");
  expect(post("{\"clockDst\":1}") == 200, "same dst saved again");
  expect(post("{\"readerStatusBarMode\":2}") == 200, "an unrelated setting saved");
  expect(timezones::applied == 2, "a page that leaves the zone alone does not touch the clock");
}
}  // namespace

int main() {
  modeAndSwitchTogether();
  switchAlone();
  modeAlone();
  zoneReachesTheClock();
  std::printf("web_post_settings:%s (%d failures)\n", failures ? "RED" : "GREEN", failures);
  return failures ? 1 : 0;
}
