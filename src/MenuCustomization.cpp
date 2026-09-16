#include "MenuCustomization.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace menucustom {
namespace {
State data;
bool loaded = false;
constexpr const char* PATH = "/.crosspoint/menu-customization.json";
constexpr const char* TEMP = "/.crosspoint/menu-customization.tmp";
constexpr const char* BACKUP = "/.crosspoint/menu-customization.bak";
constexpr const char* GROUP_NAMES[] = {"home", "settings", "reader", "text"};
constexpr int COUNTS[] = {5, 7, 4, 4};
bool read(const char* path) {
  auto file = Storage.open(path);
  if (!file || file.size() > 8192) return false;
  file.close();
  JsonDocument doc;
  const String json = Storage.readFile(path);
  if (deserializeJson(doc, json) || doc["version"].as<int>() != 1) return false;
  if (!doc["pins"].is<JsonArrayConst>() || !doc["tabs"].is<JsonObjectConst>()) return false;
  for (int g = 0; g < GROUPS; ++g) {
    if (doc["tabs"][GROUP_NAMES[g]].is<JsonArrayConst>()) {
      data.order[g].fill(255);
      int i = 0;
      for (auto v : doc["tabs"][GROUP_NAMES[g]].as<JsonArrayConst>()) {
        if (i >= MAX_TABS) break;
        if (v.is<uint8_t>()) data.order[g][i++] = v.as<uint8_t>();
      }
    }
    data.normalize(g, COUNTS[g]);
  }
  data.pinCount = 0;
  for (auto v : doc["pins"].as<JsonArrayConst>()) {
    if (data.pinCount >= MAX_PINS) break;
    if (!v.is<const char*>()) continue;
    const char* key = v.as<const char*>();
    if (!key || !*key || strlen(key) >= KEY_SIZE || !strchr(key, '/') || data.find(key) >= 0) continue;
    strcpy(data.pins[data.pinCount++].data(), key);
  }
  return true;
}
}  // namespace
State& state() { return data; }
void load() {
  if (loaded) return;
  loaded = true;
  if (!read(PATH)) read(BACKUP);
}
bool save() {
  JsonDocument doc;
  doc["version"] = 1;
  for (int g = 0; g < GROUPS; ++g) {
    auto a = doc["tabs"][GROUP_NAMES[g]].to<JsonArray>();
    for (int i = 0; i < COUNTS[g]; ++i) a.add(data.order[g][i]);
  }
  auto pins = doc["pins"].to<JsonArray>();
  for (int i = 0; i < data.pinCount; ++i) pins.add(data.pins[i].data());
  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  if (!Storage.writeFile(TEMP, json) || Storage.readFile(TEMP) != json) return false;
  if (Storage.exists(BACKUP) && !Storage.remove(BACKUP)) return false;
  const bool existed = Storage.exists(PATH);
  if (existed && !Storage.rename(PATH, BACKUP)) return false;
  if (Storage.rename(TEMP, PATH)) return true;
  if (existed) Storage.rename(BACKUP, PATH);
  return false;
}
}  // namespace menucustom
