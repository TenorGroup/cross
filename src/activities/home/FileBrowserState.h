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

// Where the File card of Home opens: the shell's last folder while `isFolder` finds it, else the root.
std::string lastFolder(bool ugly, bool (*isFolder)(const char*));
// The order of the running shell's File list, read and chosen in Settings, System.
SortMode sortNow(bool ugly);
void chooseSort(bool ugly, SortMode mode);  // memory only
// The Settings row, for the running shell, with one write after the next frame (FileBrowserActivity.cpp).
SortMode settingsSort();
void chooseSettingsSort(SortMode mode);
}  // namespace filebrowser
