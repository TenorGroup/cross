#include <cstdio>
#include <string>

#include "CrossPointSettings.h"

namespace {

bool expect(const bool condition, const char* label) {
  if (!condition) std::printf("FAIL %s\n", label);
  return condition;
}

// The toolbar is every board's default since v1.0.72 (founder 21/10); a saved list menu is still honoured.
uint8_t boardDefault() { return CrossPointSettings::READER_MENU_TOOLBAR; }

int run(const std::string& mode) {
  JsonDocument doc;
  uint8_t expected = boardDefault();
  bool ok = expect(SETTINGS.readerMenuStyle == expected, "constructor selects the board default");
  if (mode == "stored-list") {
    doc["readerMenuStyle"] = CrossPointSettings::READER_MENU_LIST;
    expected = CrossPointSettings::READER_MENU_LIST;
  } else if (mode == "stored-toolbar") {
    doc["readerMenuStyle"] = CrossPointSettings::READER_MENU_TOOLBAR;
    expected = CrossPointSettings::READER_MENU_TOOLBAR;
  } else if (mode != "absent") {
    return 2;
  }

  ok = expect(SETTINGS.fromJson(doc.as<JsonVariantConst>()), "settings JSON loads") && ok;
  ok = expect(SETTINGS.readerMenuStyle == expected, "reader menu style matches the board default or saved key") && ok;

  JsonDocument saved;
  SETTINGS.toJson(saved);
  ok = expect(saved["readerMenuStyle"].as<uint8_t>() == expected, "reader menu style survives save") && ok;
  std::printf("reader_menu_default=%s:%s\n", mode.c_str(), ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) { return argc == 2 ? run(argv[1]) : 2; }
