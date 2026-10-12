#include "FileSearchHalAdapter.h"

#include <HalStorage.h>
#include <HalMemory.h>
#include <FsHelpers.h>
#include "MappedInputManager.h"
#include <new>

namespace filesearch {
namespace {
class HalDirectory final : public Directory {
 public:
  explicit HalDirectory(HalFile directory) : directory_(std::move(directory)) {}
  bool next(ScanEntry& entry) override {
    HalFile file = directory_.openNextFile();
    if (!file) return false;
    char name[512]{};
    if (!file.getName(name, sizeof(name))) return false;
    entry.name = name;
    entry.directory = file.isDirectory();
    entry.hidden = !entry.name.empty() && entry.name.front() == '.';
    return true;
  }

 private:
  HalFile directory_;
};
}

std::unique_ptr<Directory> HalStorageAdapter::open(const std::string& path) {
  HalFile directory = Storage.open(path.c_str());
  if (!directory || !directory.isDirectory()) return {};
  directory.rewindDirectory();
  return std::unique_ptr<Directory>(new (std::nothrow) HalDirectory(std::move(directory)));
}

ScanBuffer HalScanHooks::allocate(size_t bytes) {
  auto buffer = HalMemory::allocatePsram(bytes);
  if (buffer && !heapfloor::judge(bytes, buffer.get(), "file-search-scan")) buffer.reset();
  return ScanBuffer(buffer.release(), [](uint8_t* p) { HalMemory::PsramDeleter{}(p); });
}
bool HalScanHooks::acceptFile(const std::string& name) const {
  return firmware_ ? FsHelpers::checkFileExtension(name, ".bin") : ScanHooks::acceptFile(name);
}
bool HalScanHooks::cancelled() const {
  return cancelled_ || input_.isPressed(MappedInputManager::Button::Back);
}
uint32_t HalScanHooks::nowMs() const { return millis(); }

void HalScanHooks::lockSlice() { lock_.emplace(); }

void HalScanHooks::unlockSlice() { lock_.reset(); }

}  // namespace filesearch
