#include "KOReaderSyncDebt.h"

#include <algorithm>
#include <cmath>

#include "KOReaderAutoSyncPolicy.h"

void SyncDebtList::put(SyncDebt debt) {
  remove(debt.path);
  items_.push_back(std::move(debt));
  if (items_.size() > kosyncauto::MAX_DEBTS) items_.erase(items_.begin());
}

bool SyncDebtList::remove(const std::string& path) {
  const auto it = std::find_if(items_.begin(), items_.end(), [&](const SyncDebt& d) { return d.path == path; });
  if (it == items_.end()) return false;
  items_.erase(it);
  return true;
}

void SyncDebtList::toJson(JsonDocument& doc) const {
  doc["v"] = 1;
  JsonArray list = doc["debts"].to<JsonArray>();
  for (const SyncDebt& d : items_) {
    JsonObject o = list.add<JsonObject>();
    o["path"] = d.path;
    o["spine"] = d.spineIndex;
    o["page"] = d.pageNumber;
    o["pages"] = d.totalPages;
    if (d.hasVisibleTextOffset) o["offset"] = d.visibleTextOffset;
    if (d.hasParagraphIndex) o["para"] = d.paragraphIndex;
    o["pct"] = d.percentage;
    o["at"] = d.leftAt;
  }
}

bool SyncDebtList::fromJson(JsonVariantConst doc) {
  items_.clear();
  const JsonArrayConst list = doc["debts"].as<JsonArrayConst>();
  if (list.isNull()) return false;
  for (JsonVariantConst v : list) {
    const char* path = v["path"].as<const char*>();
    if (!path || !*path) continue;
    const float pct = v["pct"] | -1.0f;
    if (!std::isfinite(pct) || pct < 0.0f || pct > 1.0f) continue;
    const uint32_t spine = v["spine"] | 0u;
    const uint32_t page = v["page"] | 0u;
    const uint32_t pages = v["pages"] | 0u;
    if (spine > UINT16_MAX || page > UINT16_MAX || pages > UINT16_MAX) continue;
    SyncDebt d;
    d.path = path;
    d.spineIndex = static_cast<uint16_t>(spine);
    d.pageNumber = static_cast<uint16_t>(page);
    d.totalPages = static_cast<uint16_t>(pages);
    d.percentage = pct;
    d.leftAt = v["at"] | 0u;
    if (v["offset"].is<uint32_t>()) {
      d.visibleTextOffset = v["offset"].as<uint32_t>();
      d.hasVisibleTextOffset = true;
    }
    if (v["para"].is<uint16_t>()) {
      d.paragraphIndex = v["para"].as<uint16_t>();
      d.hasParagraphIndex = true;
    }
    put(std::move(d));  // also enforces one entry per book and the cap
  }
  return true;
}
