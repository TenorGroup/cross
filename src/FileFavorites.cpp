#include "FileFavorites.h"

#include <HalStorage.h>

#include <cstdio>

#include "MenuCustomization.h"
namespace filefavorites {
namespace {
std::string record(const std::string& key) { return "/.crosspoint/favorite-files/" + key.substr(7) + ".txt"; }
bool safeKey(const std::string& key) {
  if (key.size() != 23 || !isFileKey(key)) return false;
  for (size_t i = 7; i < key.size(); ++i)
    if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
  return true;
}
}  // namespace
bool isFileKey(const std::string& key) { return key.rfind("folder/", 0) == 0 || key.rfind("bookid/", 0) == 0; }
std::string keyFor(const std::string& path, bool folder) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  hash ^= folder ? 1 : 0;
  char key[32];
  snprintf(key, sizeof(key), "%s%016llx", folder ? "folder/" : "bookid/", static_cast<unsigned long long>(hash));
  return key;
}
std::string pathFor(const std::string& key) {
  if (!safeKey(key)) return {};
  auto f = Storage.open(record(key).c_str());
  if (!f || f.size() == 0 || f.size() > 2048) return {};
  f.close();
  const String raw = Storage.readFile(record(key).c_str());
  const std::string path(raw.c_str(), raw.length());
  if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos ||
      keyFor(path, key.rfind("folder/", 0) == 0) != key)
    return {};
  return path;
}
bool unpin(const std::string& key) {
  if (!isFileKey(key) || menucustom::state().find(key.c_str()) < 0) return false;
  if (!menucustom::togglePin(key.c_str())) return false;
  if (safeKey(key)) Storage.remove(record(key).c_str());
  return true;
}
bool toggle(const std::string& path, bool folder) {
  if (path.empty() || path[0] != '/' || path.size() > 2048) return false;
  const auto key = keyFor(path, folder);
  if (menucustom::state().find(key.c_str()) >= 0) return unpin(key);
  if (menucustom::state().pinCount >= menucustom::MAX_PINS) return false;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir("/.crosspoint/favorite-files");
  const auto dest = record(key);
  const bool exists = Storage.exists(dest.c_str());
  if (exists && pathFor(key) != path) return false;
  if (!exists && (!Storage.writeFile(dest.c_str(), String(path.c_str())) || pathFor(key) != path)) {
    Storage.remove(dest.c_str());
    return false;
  }
  if (menucustom::togglePin(key.c_str())) return true;
  Storage.remove(dest.c_str());
  return false;
}
}  // namespace filefavorites
