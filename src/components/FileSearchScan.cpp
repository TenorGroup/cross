#include "FileSearchScan.h"

#include "FileSearch.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace filesearch {
namespace {
bool searchable(const std::string& name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = name.substr(dot);
  for (char& c : ext)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return ext == ".epub" || ext == ".xtc" || ext == ".txt" || ext == ".md" || ext == ".bmp" || ext == ".png";
}
bool naturalLess(const std::string_view left, const std::string_view right) {
  size_t a = 0, b = 0;
  while (a < left.size() && b < right.size()) {
    if (left[a] >= '0' && left[a] <= '9' && right[b] >= '0' && right[b] <= '9') {
      size_t ae = a, be = b;
      while (ae < left.size() && left[ae] >= '0' && left[ae] <= '9') ++ae;
      while (be < right.size() && right[be] >= '0' && right[be] <= '9') ++be;
      size_t az = a;
      while (az + 1 < ae && left[az] == '0') ++az;
      size_t bz = b;
      while (bz + 1 < be && right[bz] == '0') ++bz;
      if (ae - az != be - bz) return ae - az < be - bz;
      if (const int cmp = left.substr(az, ae - az).compare(right.substr(bz, be - bz)); cmp != 0) return cmp < 0;
      if (ae - a != be - b) return ae - a < be - b;
      a = ae;
      b = be;
      continue;
    }
    if (left[a] != right[b]) return left[a] < right[b];
    ++a;
    ++b;
  }
  return left.size() < right.size();
}

bool hiddenName(const ScanEntry& entry) {
  return entry.hidden || entry.name.empty() || entry.name == "." || entry.name == ".." || entry.name[0] == '.';
}
std::string joinPath(const std::string& parent, const std::string& name) {
  return parent == "/" ? "/" + name : parent + "/" + name;
}
}

bool ScanHooks::acceptFile(const std::string& name) const { return searchable(name); }

bool Scanner::start(Storage& storage, ScanHooks& hooks, std::string root, std::string query,
                    const ScanLimits& limits) {
  storage_ = &storage;
  hooks_ = &hooks;
  limits_ = limits;
  query_ = std::move(query);
  frameCount_ = resultCount_ = pathUsed_ = 0;
  stats_ = {};
  cancelled_ = false;
  reason_ = ScanReason::Error;
  auto resultStorage = heapfloor::array<StoredResult[]>(limits_.maxResults, "file-search-scan-results");
  auto pathStorage = heapfloor::array<char[]>(limits_.maxPathBytes, "file-search-scan-paths");
  if (!resultStorage || !pathStorage || limits_.maxDepth >= frames_.size()) {
    reason_ = ScanReason::Oom;
    return false;
  }
  results_ = std::move(resultStorage);
  pathPool_ = std::move(pathStorage);
  startedMs_ = hooks.nowMs();
  if (!pushFrame(root.empty() ? "/" : std::move(root), 0)) {
    reason_ = ScanReason::Error;
    return false;
  }
  reason_ = ScanReason::Running;
  return true;
}

bool Scanner::pushFrame(const std::string& path, const uint8_t depth) {
  if (frameCount_ >= frames_.size()) return false;
  auto directory = storage_->open(path);
  if (!directory) return false;
  frames_[frameCount_++] = {std::move(directory), path, depth};
  stats_.depth = std::max(stats_.depth, depth);
  return true;
}

bool Scanner::better(const StoredResult& left, const StoredResult& right) const {
  if (left.rank != right.rank) return left.rank < right.rank;
  const std::string_view a(pathPool_.get() + left.pathOffset);
  const std::string_view b(pathPool_.get() + right.pathOffset);
  if (naturalLess(a, b)) return true;
  return !naturalLess(b, a) && a < b;
}

bool Scanner::addResult(const std::string& path, const uint8_t rank) {
  const auto candidateBetter = [&](const std::string& candidate, const uint8_t candidateRank,
                                   const StoredResult& current) {
    if (candidateRank != current.rank) return candidateRank < current.rank;
    const std::string_view existing(pathPool_.get() + current.pathOffset);
    if (naturalLess(candidate, existing)) return true;
    return !naturalLess(existing, candidate) && candidate < existing;
  };
  size_t replace = resultCount_;
  if (resultCount_ == limits_.maxResults) {
    size_t worst = 0;
    for (size_t i = 1; i < resultCount_; ++i)
      if (better(results_[worst], results_[i])) worst = i;
    if (!candidateBetter(path, rank, results_[worst])) return true;
    replace = worst;
  }
  if (path.size() + 1 > limits_.maxPathBytes - pathUsed_) {
    size_t compacted = 0;
    for (size_t i = 0; i < resultCount_; ++i) {
      if (i == replace) continue;
      const char* old = pathPool_.get() + results_[i].pathOffset;
      const size_t length = strlen(old) + 1;
      std::memmove(pathPool_.get() + compacted, old, length);
      results_[i].pathOffset = static_cast<uint32_t>(compacted);
      compacted += length;
    }
    pathUsed_ = compacted;
  }
  if (path.size() + 1 > limits_.maxPathBytes - pathUsed_) return false;
  const uint32_t offset = static_cast<uint32_t>(pathUsed_);
  std::copy(path.begin(), path.end(), pathPool_.get() + pathUsed_);
  pathPool_[pathUsed_ + path.size()] = '\0';
  pathUsed_ += path.size() + 1;
  StoredResult value{offset, rank};
  if (replace == resultCount_) ++resultCount_;
  results_[replace] = value;
  std::sort(results_.get(), results_.get() + resultCount_, [this](const StoredResult& a, const StoredResult& b) {
    return better(a, b);
  });
  return true;
}

void Scanner::finish(const ScanReason reason) {
  reason_ = reason;
  for (size_t i = 0; i < frames_.size(); ++i) frames_[i] = {};
  frameCount_ = 0;
}

ScanReason Scanner::tick() {
  if (reason_ != ScanReason::Running) return reason_;
  const uint32_t sliceStart = hooks_->nowMs();
  hooks_->lockSlice();
  uint8_t processed = 0;
  while (frameCount_ > 0 && processed < limits_.sliceEntries) {
    if (cancelled_ || hooks_->cancelled()) {
      stats_.cancelMs = hooks_->nowMs() - startedMs_;
      finish(ScanReason::Cancelled);
      break;
    }
    if (stats_.entries >= limits_.maxEntries) {
      finish(ScanReason::Limit);
      break;
    }
    if (hooks_->nowMs() - startedMs_ >= limits_.totalMs) {
      finish(ScanReason::Timeout);
      break;
    }
    Frame& frame = frames_[frameCount_ - 1];
    ScanEntry entry;
    const uint32_t readStart = hooks_->nowMs();
    ++stats_.readCalls;
    if (!frame.directory->next(entry)) {
      frames_[--frameCount_] = {};
      continue;
    }
    stats_.maxBlockMs = std::max(stats_.maxBlockMs, hooks_->nowMs() - readStart);
    ++stats_.entries;
    ++processed;
    if (hiddenName(entry)) continue;
    const std::string path = joinPath(frame.path, entry.name);
    if (entry.directory) {
      if (frame.depth < limits_.maxDepth && !pushFrame(path, static_cast<uint8_t>(frame.depth + 1))) {
        finish(ScanReason::Oom);
        break;
      }
      continue;
    }
    if (!hooks_->acceptFile(entry.name)) continue;
    const uint8_t rank = matchRank(path, query_);
    if (rank != 255 && !addResult(path, rank)) {
      finish(ScanReason::Oom);
      break;
    }
    if (hooks_->nowMs() - sliceStart >= limits_.sliceMs) break;
  }
  hooks_->unlockSlice();
  stats_.sliceMs = std::max(stats_.sliceMs, hooks_->nowMs() - sliceStart);
  if (reason_ == ScanReason::Running && frameCount_ == 0) finish(ScanReason::Complete);
  return reason_;
}

void Scanner::cancel() { cancelled_ = true; }

ScanResult Scanner::result(const size_t index) const {
  if (index >= resultCount_) return {};
  return {pathPool_.get() + results_[index].pathOffset, results_[index].rank};
}

}  // namespace filesearch
