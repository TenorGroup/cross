#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace menucustom {
constexpr int GROUPS = 4;
constexpr int MAX_TABS = 8;
constexpr int MAX_PINS = 32;
constexpr int KEY_SIZE = 64;
struct State {
  std::array<std::array<uint8_t, MAX_TABS>, GROUPS> order{};
  std::array<std::array<char, KEY_SIZE>, MAX_PINS> pins{};
  uint8_t pinCount = 0;
  State() {
    for (auto& group : order)
      for (int i = 0; i < MAX_TABS; ++i) group[i] = i;
    order[0] = {0, 1, 4, 2, 3, 5, 6, 7};
  }
  void normalize(int group, int count) {
    std::array<uint8_t, MAX_TABS> result{};
    std::array<bool, MAX_TABS> seen{};
    int n = 0;
    for (auto id : order[group])
      if (id < count && !seen[id]) {
        result[n++] = id;
        seen[id] = true;
      }
    for (int id = 0; id < count; ++id)
      if (!seen[id]) result[n++] = id;
    order[group] = result;
  }
  int find(const char* key) const {
    if (!key || !*key) return -1;
    for (int i = 0; i < pinCount; ++i)
      if (strcmp(pins[i].data(), key) == 0) return i;
    return -1;
  }
};
State& state();
void load();
bool save();
inline int groupFor(const char* name) {
  if (strcmp(name, "Home") == 0) return 0;
  if (strcmp(name, "Settings") == 0) return 1;
  if (strcmp(name, "EpubReaderMenu") == 0) return 2;
  if (strcmp(name, "TextSettings") == 0) return 3;
  return -1;
}
inline int position(int group, int id, int count) {
  if (group < 0 || group >= GROUPS) return id;
  for (int i = 0; i < count; ++i)
    if (state().order[group][i] == id) return i;
  return 0;
}
inline int idAt(int group, int pos, int count) {
  if (pos < 0 || pos >= count) return 0;
  return group >= 0 && group < GROUPS ? state().order[group][pos] : pos;
}
inline int adjacent(int group, int id, int count, int direction) {
  return idAt(group, (position(group, id, count) + direction + count) % count, count);
}
inline bool moveTab(int group, int id, int count, int direction) {
  if (group < 0 || group >= GROUPS) return false;
  const int from = position(group, id, count), to = from + direction;
  if (to < 0 || to >= count) return false;
  std::swap(state().order[group][from], state().order[group][to]);
  if (save()) return true;
  std::swap(state().order[group][from], state().order[group][to]);
  return false;
}
inline bool togglePin(const char* key) {
  if (!key || !*key || strlen(key) >= KEY_SIZE) return false;
  const int old = state().find(key);
  if (old < 0) {
    if (state().pinCount == MAX_PINS) return false;
    strcpy(state().pins[state().pinCount++].data(), key);
    if (save()) return true;
    --state().pinCount;
  } else {
    char removed[KEY_SIZE];
    strcpy(removed, state().pins[old].data());
    for (int i = old; i + 1 < state().pinCount; ++i) state().pins[i] = state().pins[i + 1];
    --state().pinCount;
    if (save()) return true;
    for (int i = state().pinCount; i > old; --i) state().pins[i] = state().pins[i - 1];
    strcpy(state().pins[old].data(), removed);
    ++state().pinCount;
  }

  return false;
}
inline bool movePin(const char* key, int direction) {
  const int from = state().find(key), to = from + direction;
  if (from < 0 || to < 0 || to >= state().pinCount) return false;
  std::swap(state().pins[from], state().pins[to]);
  if (save()) return true;
  std::swap(state().pins[from], state().pins[to]);
  return false;
}
}  // namespace menucustom
