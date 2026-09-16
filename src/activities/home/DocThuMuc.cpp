#include "DocThuMuc.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <cstring>

namespace docthumuc {
namespace {
bool visible(const char* name, bool showHidden) {
  return name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && (showHidden || name[0] != '.') &&
         strcmp(name, "System Volume Information") != 0;
}
}  // namespace

bool doc(const char* const duongDan, const bool hienFileAn, const Loc loc, char* const dem, const size_t demCo,
         std::vector<std::string>& ra) {
  ra.clear();
  if (dem == nullptr || demCo == 0) return false;

  auto goc = Storage.open(duongDan);
  if (!goc || !goc.isDirectory()) return false;
  goc.rewindDirectory();

  for (auto muc = goc.openNextFile(); muc; muc = goc.openNextFile()) {
    muc.getName(dem, demCo);
    // File an, va thu muc rac cua Windows.
    if (!visible(dem, hienFileAn)) continue;

    if (muc.isDirectory()) {
      ra.emplace_back(std::string(dem) + "/");
      continue;
    }

    const std::string_view ten{dem};
    if (loc == Loc::Firmware) {
      if (FsHelpers::checkFileExtension(ten, ".bin")) ra.emplace_back(ten);
      continue;
    }
    if (FsHelpers::hasEpubExtension(ten) || FsHelpers::hasXtcExtension(ten) || FsHelpers::hasTxtExtension(ten) ||
        FsHelpers::hasMarkdownExtension(ten) || FsHelpers::hasBmpExtension(ten) || FsHelpers::hasPngExtension(ten)) {
      ra.emplace_back(ten);
    }
  }
  goc.close();
  FsHelpers::sortFileList(ra);
  return true;
}

std::string neighbor(const std::string& path, bool showHidden, int direction, char* buffer, size_t capacity) {
  if (path.empty() || path == "/" || !buffer || !capacity) return {};
  const auto slash = path.find_last_of('/');
  const std::string parent = slash == 0 ? "/" : path.substr(0, slash);
  const std::string current = path.substr(slash + 1);
  auto dir = Storage.open(parent.c_str());
  if (!dir || !dir.isDirectory()) return {};
  std::string adjacent, edge;
  const auto before = [direction](const std::string& a, const std::string& b) {
    return direction > 0 ? FsHelpers::naturalLess(a, b) : FsHelpers::naturalLess(b, a);
  };
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) continue;
    entry.getName(buffer, capacity);
    if (!visible(buffer, showHidden)) continue;
    const std::string name(buffer);
    if (edge.empty() || before(name, edge)) edge = name;
    if (before(current, name) && (adjacent.empty() || before(name, adjacent))) adjacent = name;
  }
  const auto& selected = adjacent.empty() ? edge : adjacent;
  return selected.empty() ? std::string{} : parent + (parent == "/" ? "" : "/") + selected;
}

}  // namespace docthumuc
