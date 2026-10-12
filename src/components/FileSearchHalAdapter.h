#pragma once

#include "FileSearchScan.h"
#include <HalPowerManager.h>

#include <memory>
#include <optional>

class MappedInputManager;

namespace filesearch {

class HalStorageAdapter final : public SearchStorage {
 public:
  std::unique_ptr<Directory> open(const std::string& path) override;
};

class HalScanHooks final : public ScanHooks {
 public:
  explicit HalScanHooks(bool& cancelled, bool hidden, MappedInputManager& input, bool firmware = false)
      : cancelled_(cancelled), hidden_(hidden), firmware_(firmware), input_(input) {}
  uint32_t nowMs() const override;
  bool cancelled() const override;
  bool showHidden() const override { return hidden_; }
  bool acceptFile(const std::string& name) const override;
  ScanBuffer allocate(size_t bytes) override;
  void lockSlice() override;
  void unlockSlice() override;

 private:
  bool& cancelled_;
  bool hidden_, firmware_;
  MappedInputManager& input_;
  std::optional<HalPowerManager::Lock> lock_;
};

}  // namespace filesearch
