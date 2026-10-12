#include "FileSearchScan.h"
#include <algorithm>
#include <cstring>
#ifdef TENOR_PRESS_PROBE
#include <Logging.h>
#endif

namespace filesearch {
bool ScanHooks::acceptFile(const std::string& name) const {
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = name.substr(dot);
  for (char& c : ext)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return ext == ".epub" || ext == ".xtc" || ext == ".txt" || ext == ".md" || ext == ".bmp" || ext == ".png";
}
ScanBuffer ScanHooks::allocate(size_t bytes) {
  return ScanBuffer(static_cast<uint8_t*>(heapfloor::take(bytes, "file-search-scan")), [](uint8_t* p) { free(p); });
}
bool Scanner::start(SearchStorage& storage, ScanHooks& hooks, const std::string& root, const std::string& query,
                    const ScanLimits& limits) {
  for (auto& frame : frames_) frame = {};
  buffer_.reset();
  storage_ = &storage;
  hooks_ = &hooks;
  limits_ = limits;
  query_ = Query(query);
  frameCount_ = resultCount_ = pathUsed_ = 0;
  stats_ = {};
  cancelled_ = incomplete_ = false;
  reason_ = ScanReason::Oom;
  if (!limits.maxResults || limits.maxResults > 256 || limits.maxDepth > 5 ||
      !limits.sliceEntries || !limits.sliceMs || root.size() > 4000 || limits.maxPathBytes > 65536 || !limits.maxPathBytes) return false;
  buffer_ = hooks.allocate(sizeof(StoredResult) * limits.maxResults + limits.maxPathBytes);
  if (!buffer_) return false;
  results_ = reinterpret_cast<StoredResult*>(buffer_.get());
  for (size_t i = 0; i < limits.maxResults; ++i) new (results_ + i) StoredResult{};
  pathPool_ = reinterpret_cast<char*>(results_ + limits.maxResults);
  // Opening the root is part of the first bounded, power-locked slice.
  frames_[0].path = root.empty() ? "/" : root;
  frames_[0].depth = 0;
  frameCount_ = 1;
  startedMs_ = hooks.nowMs();
  reason_ = ScanReason::Running;
  return true;
}
bool Scanner::pushFrame(const std::string& path, uint8_t depth) {
  if (frameCount_ == frames_.size()) return false;
  frames_[frameCount_++] = {nullptr, path, depth};
  stats_.depth = std::max(stats_.depth, depth);
  return true;
}
bool Scanner::better(const StoredResult& left, const StoredResult& right) const {
  return left.rank != right.rank ? left.rank < right.rank
                                : nameLess(pathPool_ + left.pathOffset, pathPool_ + right.pathOffset);
}
bool Scanner::addResult(const std::string& path, uint8_t rank) {
  size_t replace = resultCount_;
  if (resultCount_ == limits_.maxResults) {
    incomplete_ = true;
    replace = resultCount_ - 1;
    const auto& worst = results_[replace];
    if (rank > worst.rank || (rank == worst.rank && !nameLess(path, pathPool_ + worst.pathOffset))) return true;
  }
  if (path.size() + 1 > limits_.maxPathBytes - pathUsed_) {
    // Move in address order: rank order may overwrite a path still waiting to move.
    std::array<uint16_t, 256> order{};
    for (size_t i = 0; i < resultCount_; ++i) order[i] = static_cast<uint16_t>(i);
    std::sort(order.begin(), order.begin() + resultCount_, [this](uint16_t a, uint16_t b) {
      return results_[a].pathOffset < results_[b].pathOffset;
    });
    size_t used = 0;
    for (size_t i = 0; i < resultCount_; ++i) {
      auto& item = results_[order[i]];
      const size_t length = strlen(pathPool_ + item.pathOffset) + 1;
      std::memmove(pathPool_ + used, pathPool_ + item.pathOffset, length);
      item.pathOffset = static_cast<uint32_t>(used);
      used += length;
    }
    pathUsed_ = used;
  }
  if (path.size() + 1 > limits_.maxPathBytes - pathUsed_) return false;
  const auto offset = static_cast<uint32_t>(pathUsed_);
  std::memcpy(pathPool_ + pathUsed_, path.c_str(), path.size() + 1);
  pathUsed_ += path.size() + 1;
  results_[replace] = {offset, rank};
  if (replace == resultCount_) ++resultCount_;
  // The accepted candidate is the only new item in an already sorted top list.
  const auto value = results_[replace];
  const auto where = std::lower_bound(results_, results_ + replace, value,
                                     [this](const StoredResult& a, const StoredResult& b) { return better(a, b); });
  std::move_backward(where, results_ + replace, results_ + replace + 1);
  *where = value;
  return true;
}
void Scanner::finish(ScanReason reason) {
  reason_ = reason;
  for (auto& frame : frames_) frame = {};
  frameCount_ = 0;
}
ScanReason Scanner::tick() {
  if (reason_ != ScanReason::Running) return reason_;
  const uint32_t sliceStart = hooks_->nowMs();
  hooks_->lockSlice();
  uint8_t processed = 0;
  while (frameCount_) {
    if (cancelled_ || hooks_->cancelled()) {
      stats_.cancelMs = cancelled_ ? hooks_->nowMs() - cancelledMs_ : 0;
      finish(ScanReason::Cancelled);
      break;
    }
    if (hooks_->nowMs() - startedMs_ >= limits_.totalMs) { finish(ScanReason::Timeout); break; }
    if (stats_.entries >= limits_.maxEntries) { finish(ScanReason::Limit); break; }
    if (processed >= limits_.sliceEntries || hooks_->nowMs() - sliceStart >= limits_.sliceMs) break;
    Frame& frame = frames_[frameCount_ - 1];
    const uint32_t readStart = hooks_->nowMs();
    ++stats_.readCalls;
    if (!frame.directory) {
      frame.directory = storage_->open(frame.path);
      stats_.maxBlockMs = std::max(stats_.maxBlockMs, hooks_->nowMs() - readStart);
      if (!frame.directory) { finish(ScanReason::Error); break; }
      continue;
    }
    ScanEntry entry;
    const bool got = frame.directory->next(entry);
    stats_.maxBlockMs = std::max(stats_.maxBlockMs, hooks_->nowMs() - readStart);
    if (hooks_->cancelled() || cancelled_) continue;
    if (!got) { frames_[--frameCount_] = {}; continue; }
    ++stats_.entries;
    ++processed;
    if (entry.name.empty() || entry.name == "." || entry.name == ".." || entry.name == "System Volume Information" ||
        (!hooks_->showHidden() && (entry.hidden || entry.name.front() == '.'))) continue;
    if (entry.name.size() > 511 || entry.name.find('/') != std::string::npos) { finish(ScanReason::Error); break; }
    const size_t pathLength = frame.path.size() + (frame.path == "/" ? 0 : 1) + entry.name.size();
    if (pathLength > 4000) { finish(ScanReason::Limit); break; }
    std::string path;
    path.reserve(pathLength);
    path.append(frame.path);
    if (frame.path != "/") path.push_back('/');
    path.append(entry.name);
    if (entry.directory) {
      if (frame.depth == limits_.maxDepth) incomplete_ = true;
      else pushFrame(path, static_cast<uint8_t>(frame.depth + 1));
      continue;
    }
    if (!hooks_->acceptFile(entry.name)) continue;
    const uint8_t rank = query_.rank(entry.name);
    if (rank != 255 && !addResult(path, rank)) { finish(ScanReason::Oom); break; }
  }
  if (reason_ == ScanReason::Running && !frameCount_) finish(incomplete_ ? ScanReason::Limit : ScanReason::Complete);
  hooks_->unlockSlice();
  stats_.sliceMs = std::max(stats_.sliceMs, hooks_->nowMs() - sliceStart);
#ifdef TENOR_PRESS_PROBE
  if (reason_ != ScanReason::Running)
    LOG_INF("SEARCH_SCAN", "entries=%u depth=%u read_calls=%lu slice_ms=%lu max_block_ms=%lu cancel_ms=%lu reason=%u",
            unsigned(stats_.entries), unsigned(stats_.depth), static_cast<unsigned long>(stats_.readCalls),
            static_cast<unsigned long>(stats_.sliceMs), static_cast<unsigned long>(stats_.maxBlockMs),
            static_cast<unsigned long>(stats_.cancelMs), unsigned(reason_));
#endif
  return reason_;
}
void Scanner::cancel() {
  if (!cancelled_) cancelledMs_ = hooks_ ? hooks_->nowMs() : 0;
  cancelled_ = true;
}
ScanResult Scanner::result(size_t index) const {
  if (index >= resultCount_) return {};
  return {pathPool_ + results_[index].pathOffset, results_[index].rank};
}
}  // namespace filesearch
