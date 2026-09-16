#pragma once
#include <array>
#include <cstdint>
#include <string>

// Session-only cursor data. Never retain activities, files, callbacks or label pointers.
struct MenuNavigationState {
  struct Cursor {
    int selected = 0;
    int top = 0;
  };
  std::array<Cursor, 8> cursors{};
  int count = 0;
  int tab = 0;
  int committedTab = -1;
  std::string location;
  std::string selection;
};

class MenuNavigationMemory {
 public:
  static constexpr size_t CAPACITY = 16;
  void enter(const std::string& parent, const std::string& child) {
    if (parent.empty() || parent == child) return;
    auto& p = slot(parent);
    if (!p.child.empty() && p.child != child) eraseBranch(p.child);
    // eraseBranch can clear the slot referenced by a malformed cyclic route.
    slot(parent).child = child;
  }
  void save(const std::string& key, const MenuNavigationState& state) {
    auto& e = slot(key);
    e.state = state;
    e.saved = true;
  }
  bool load(const std::string& key, MenuNavigationState& state) const {
    for (const auto& e : entries)
      if (e.key == key && e.saved) {
        state = e.state;
        return true;
      }
    return false;
  }

 private:
  struct Entry {
    std::string key;
    std::string child;
    MenuNavigationState state;
    bool saved = false;
  };
  std::array<Entry, CAPACITY> entries{};
  size_t replacement = 0;
  Entry& slot(const std::string& key) {
    for (auto& e : entries)
      if (e.key == key) return e;
    for (auto& e : entries)
      if (e.key.empty()) {
        e.key = key;
        return e;
      }
    auto& e = entries[replacement];
    replacement = (replacement + 1) % CAPACITY;
    e = {};
    e.key = key;
    return e;
  }
  void eraseBranch(std::string key) {
    // Bound traversal even if a caller accidentally supplies a cyclic route.
    for (size_t depth = 0; depth < CAPACITY && !key.empty(); ++depth) {
      bool found = false;
      for (auto& e : entries)
        if (e.key == key) {
          key = e.child;
          e = {};
          found = true;
          break;
        }
      if (!found) break;
    }
  }
};
