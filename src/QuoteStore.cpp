#include "QuoteStore.h"

#include <HalStorage.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
namespace quotes {
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/quotes";
}
bool load(const std::string& name, QuoteRecord& q) {
  if (name.size() != 21 || name.substr(16) != ".json" || name.find_first_not_of("0123456789abcdef", 0) != 16)
    return false;
  JsonDocument doc;
  const std::string filePath = std::string(DIRECTORY) + "/" + name;
  {
    auto file = Storage.open(filePath.c_str());
    if (!file || file.size() > 16384) return false;
  }
  if (!PersistableStoreBase::readDocFromFile(filePath.c_str(), doc)) return false;
  if ((doc["schema"] | 0) != 1) return false;
  const char* text = doc["text"] | "";
  const char* path = doc["path"] | "";
  const char* title = doc["title"] | "";
  if (!*text || strlen(text) > MAX_BYTES || strlen(path) > 1024 || strlen(title) > 512) return false;
  q.text = text;
  q.path = path;
  q.title = title;
  q.spine = doc["spine"] | 0;
  q.page = doc["page"] | 0;
  q.day = doc["day"] | 0u;
  return true;
}
bool save(const QuoteRecord& q) {
  if (q.text.empty() || q.text.size() > MAX_BYTES || q.path.empty() || q.path.size() > 1024 || q.title.size() > 512)
    return false;
  uint64_t hash = 14695981039346656037ULL;
  for (const auto* part : {&q.path, &q.text}) {
    for (const unsigned char c : *part) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
    hash ^= 0;
    hash *= 1099511628211ULL;
  }
  for (int value : {q.spine, q.page}) {
    hash ^= static_cast<uint32_t>(value);
    hash *= 1099511628211ULL;
  }
  char name[24];
  snprintf(name, sizeof(name), "%016llx.json", static_cast<unsigned long long>(hash));
  const std::string path = std::string(DIRECTORY) + "/" + name;
  if (Storage.exists(path.c_str())) {
    QuoteRecord existing;
    return load(name, existing) && existing.path == q.path && existing.text == q.text && existing.spine == q.spine &&
           existing.page == q.page;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !Storage.ensureDirectoryExists(DIRECTORY)) return false;
  JsonDocument doc;
  doc["schema"] = 1;
  doc["path"] = q.path;
  doc["title"] = q.title;
  doc["text"] = q.text;
  doc["spine"] = q.spine;
  doc["page"] = q.page;
  doc["day"] = q.day;
  const std::string temporary = path + ".tmp";
  if (doc.overflowed() || !PersistableStoreBase::writeDocToFile(temporary.c_str(), doc)) return false;
  return Storage.rename(temporary.c_str(), path.c_str());
}
void list(const std::string& boundary, const bool previous, std::vector<std::string>& names) {
  names.clear();
  names.reserve(PAGE_SIZE + 1);
  auto directory = Storage.open(DIRECTORY);
  if (!directory || !directory.isDirectory()) return;
  char name[32];
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name, sizeof(name));
    std::string key = name;
    if (key.size() != 21 || key.substr(16) != ".json") continue;
    if (!boundary.empty() && (previous ? key >= boundary : key <= boundary)) continue;
    names.insert(std::lower_bound(names.begin(), names.end(), key), std::move(key));
    if (names.size() > PAGE_SIZE + 1) {
      if (previous)
        names.erase(names.begin());
      else
        names.pop_back();
    }
  }
}
}  // namespace quotes
