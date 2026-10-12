#pragma once

#include <string>
#include "FileBrowserSort.h"

namespace filebrowser {
struct State {
  std::string crossPath = "/", uglyPath = "/";
  SortMode crossSort = SortMode::Name, uglySort = SortMode::Name;
};

// Main loop only. Visiting folders changes RAM; the closing screen queues one write.
class Store {
 public:
  const State& get();
  void note(bool ugly, const std::string& path, SortMode mode);
  bool flush();

 private:
  State data;
  bool loaded = false, dirty = false;
};
const State& state();
void note(bool ugly, const std::string& path, SortMode mode);
void flush();
}  // namespace filebrowser
