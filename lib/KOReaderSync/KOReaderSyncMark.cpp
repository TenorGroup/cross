#include "KOReaderSyncMark.h"

#include <algorithm>

#include "KOReaderAutoSyncPolicy.h"

void SyncMarkList::put(SyncMark mark) {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&](const SyncMark& m) { return m.document == mark.document; }),
               items_.end());
  items_.push_back(std::move(mark));
  if (items_.size() > kosyncauto::MAX_MARKS) items_.erase(items_.begin());
}

const SyncMark* SyncMarkList::find(const std::string& document) const {
  const auto it = std::find_if(items_.begin(), items_.end(), [&](const SyncMark& m) { return m.document == document; });
  return it == items_.end() ? nullptr : &*it;
}

bool SyncMarkList::serverIsOurs(const std::string& document, const std::string& progress, const float percentage) const {
  const SyncMark* mark = find(document);
  return mark && mark->token == kosyncauto::recordToken(progress, percentage);
}

bool SyncMarkList::placeChanged(const std::string& document, const uint32_t place) const {
  const SyncMark* mark = find(document);
  return !mark || mark->place != place;
}

void SyncMarkList::toJson(JsonDocument& doc) const {
  doc["v"] = 1;
  JsonArray list = doc["marks"].to<JsonArray>();
  for (const SyncMark& m : items_) {
    JsonObject o = list.add<JsonObject>();
    o["d"] = m.document;
    o["t"] = m.token;
    o["p"] = m.place;
  }
}

bool SyncMarkList::fromJson(JsonVariantConst doc) {
  items_.clear();
  const JsonArrayConst list = doc["marks"].as<JsonArrayConst>();
  if (list.isNull()) return false;
  for (JsonVariantConst v : list) {
    const char* document = v["d"].as<const char*>();
    if (!document || !*document || !v["t"].is<uint32_t>() || !v["p"].is<uint32_t>()) continue;
    put({document, v["t"].as<uint32_t>(), v["p"].as<uint32_t>()});
  }
  return true;
}
