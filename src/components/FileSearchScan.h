#pragma once

#include "FileSearch.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace filesearch {
struct ScanEntry {
  std::string name;
  bool directory = false;
  bool hidden = false;
};
class Directory {
 public:
  virtual ~Directory() = default;
  virtual bool next(ScanEntry& entry) = 0;
};
class SearchStorage {
 public:
  virtual ~SearchStorage() = default;
  virtual std::unique_ptr<Directory> open(const std::string& path) = 0;
};
struct ScanLimits {
  uint8_t maxDepth = 5;
  uint16_t maxEntries = 8192;
  uint16_t maxResults = 256;
  uint32_t maxPathBytes = 64 * 1024;
  uint16_t totalMs = 2000;
  uint8_t sliceEntries = 32;
  uint8_t sliceMs = 20;
};
using ScanBuffer = std::unique_ptr<uint8_t[], void (*)(uint8_t*)>;
struct ScanHooks {
  virtual ~ScanHooks() = default;
  virtual uint32_t nowMs() const = 0;
  virtual bool cancelled() const = 0;
  virtual bool acceptFile(const std::string& name) const;
  virtual bool showHidden() const { return false; }
  virtual ScanBuffer allocate(size_t bytes);
  virtual void lockSlice() {}
  virtual void unlockSlice() {}
};
struct ScanStats {
  uint16_t entries = 0;
  uint8_t depth = 0;
  uint32_t readCalls = 0;
  uint32_t sliceMs = 0;
  uint32_t maxBlockMs = 0;
  uint32_t cancelMs = 0;
};
enum class ScanReason : uint8_t { Running, Complete, Limit, Timeout, Cancelled, Oom, Error };
struct ScanResult {
  const char* path = nullptr;
  uint8_t rank = 255;
};
class Scanner {
 public:
  bool start(SearchStorage& storage, ScanHooks& hooks, const std::string& root, const std::string& query,
             const ScanLimits& limits = {});
  ScanReason tick();
  void cancel();
  ScanReason reason() const { return reason_; }
  const ScanStats& stats() const { return stats_; }
  size_t resultCount() const { return resultCount_; }
  size_t allocatedBytes() const { return buffer_ ? sizeof(StoredResult) * limits_.maxResults + limits_.maxPathBytes : 0; }
  ScanResult result(size_t index) const;
 private:
  struct Frame {
    std::unique_ptr<Directory> directory;
    std::string path;
    uint8_t depth = 0;
  };
  struct StoredResult { uint32_t pathOffset; uint8_t rank; };
  SearchStorage* storage_ = nullptr;
  ScanHooks* hooks_ = nullptr;
  ScanLimits limits_{};
  Query query_{""};
  std::array<Frame, 6> frames_{};
  size_t frameCount_ = 0;
  ScanBuffer buffer_{nullptr, [](uint8_t* p) { free(p); }};
  StoredResult* results_ = nullptr;
  char* pathPool_ = nullptr;
  size_t resultCount_ = 0, pathUsed_ = 0;
  uint32_t startedMs_ = 0, cancelledMs_ = 0;
  ScanStats stats_{};
  ScanReason reason_ = ScanReason::Error;
  bool cancelled_ = false, incomplete_ = false;
  bool pushFrame(const std::string& path, uint8_t depth);
  bool addResult(const std::string& path, uint8_t rank);
  bool better(const StoredResult& left, const StoredResult& right) const;
  void finish(ScanReason reason);
};
}  // namespace filesearch
