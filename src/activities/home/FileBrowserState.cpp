#include "FileBrowserState.h"

#include <PersistableStore.h>
#include <cstring>

namespace filebrowser {
namespace {
constexpr char PATH[] = "/.crosspoint/file-browser.json";
constexpr size_t MAX_PATH = 255;
Store store;

bool validPath(const char* path) {
  if (!path || path[0] != '/' || strlen(path) > MAX_PATH || strchr(path, '\\')) return false;
  if (!path[1]) return true;
  for (const char* start = path + 1; *start;) {
    const char* end = strchr(start, '/');
    const size_t size = end ? static_cast<size_t>(end - start) : strlen(start);
    if (!size || (size == 1 && start[0] == '.') || (size == 2 && start[0] == '.' && start[1] == '.'))
      return false;
    if (!end) return true;
    start = end + 1;
    if (!*start) return false;
  }
  return true;
}
const char* pathOrRoot(JsonVariantConst value) {
  const char* path = value | "/";
  return validPath(path) ? path : "/";
}
SortMode readSort(JsonVariantConst value) {
  return value.is<int>() ? sortModeFromValue(value.as<int>()) : SortMode::Name;
}
}  // namespace

const State& Store::get() {
  if (loaded) return data;
  loaded = true;
  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileStatus(PATH, doc, 2048) != PersistableStoreBase::ReadResult::Ready ||
      !doc["version"].is<int>() || doc["version"].as<int>() != 1) return data;
  data.crossPath = pathOrRoot(doc["crossPath"]);
  data.uglyPath = pathOrRoot(doc["uglyPath"]);
  data.crossSort = readSort(doc["crossSort"]);
  data.uglySort = readSort(doc["uglySort"]);
  return data;
}
void Store::note(bool ugly, const std::string& path, SortMode mode) {
  get();
  if (!validPath(path.c_str()) || path.size() != strlen(path.c_str())) return;
  auto& oldPath = ugly ? data.uglyPath : data.crossPath;
  auto& oldMode = ugly ? data.uglySort : data.crossSort;
  mode = sortModeFromValue(sortModeValue(mode));
  if (oldPath == path && oldMode == mode) return;
  oldPath = path;
  oldMode = mode;
  dirty = true;
}
bool Store::flush() {
  if (!dirty) return true;
  JsonDocument doc;
  doc["version"] = 1;
  doc["crossPath"] = data.crossPath.c_str();
  doc["uglyPath"] = data.uglyPath.c_str();
  doc["crossSort"] = sortModeValue(data.crossSort);
  doc["uglySort"] = sortModeValue(data.uglySort);
  // The shared writer stages, syncs and closes .davtmp before the rename, retaining .davbak on interruption.
  if (!PersistableStoreBase::writeDocToFile(PATH, doc)) return false;
  dirty = false;
  return true;
}
const State& state() { return store.get(); }
void note(bool ugly, const std::string& path, SortMode mode) { store.note(ugly, path, mode); }
void flush() { store.flush(); }
std::string lastFolder(const bool ugly, bool (*isFolder)(const char*)) {
  const State& saved = store.get();
  const std::string& path = ugly ? saved.uglyPath : saved.crossPath;
  return path != "/" && isFolder(path.c_str()) ? path : "/";
}
SortMode sortNow(const bool ugly) { return ugly ? store.get().uglySort : store.get().crossSort; }
void chooseSort(const bool ugly, const SortMode mode) {
  const State& saved = store.get();
  store.note(ugly, ugly ? saved.uglyPath : saved.crossPath, mode);
}
}  // namespace filebrowser
