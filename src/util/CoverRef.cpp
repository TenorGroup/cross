#include "CoverRef.h"

#include <HalStorage.h>

bool coverref::save(const std::string& cachePath, const std::string& href) {
  uint8_t bytes[MAX_BYTES];
  const size_t size = encode(href, bytes, sizeof(bytes));
  if (size == 0) return false;
  const auto target = path(cachePath);
  const auto part = target + ".tmp";
  Storage.remove(part.c_str());
  HalFile file;
  bool ok = Storage.openFileForWrite("REF", part, file) && file.write(bytes, size) == size;
  file.close();
  // A file of another version stands in the way of the rename.
  if (ok) Storage.remove(target.c_str());
  ok = ok && Storage.rename(part.c_str(), target.c_str());
  if (!ok) Storage.remove(part.c_str());
  return ok;
}

bool coverref::load(const std::string& cachePath, std::string& href) {
  HalFile file;
  if (!Storage.openFileForRead("REF", path(cachePath), file)) return false;
  uint8_t bytes[MAX_BYTES + 1];
  const int size = file.read(bytes, sizeof(bytes));
  file.close();
  return size > 0 && decode(bytes, static_cast<size_t>(size), href);
}
