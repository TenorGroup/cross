#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "activities/settings/SettingsTabs.h"

namespace menucustom {
constexpr int GROUPS = 4;
constexpr int MAX_TABS = 9;
constexpr int MAX_PINS = 32;
constexpr int KEY_SIZE = 64;
// Keep saved v1 pin keys intact while matching their current setting routes.
// Shared by pin membership, duplicate detection and the favorites launcher.
inline const char* canonicalPinKey(const char* key) {
  if (!key) return "";
  if (strcmp(key, "text/focusReadingEnabled") == 0) return "text/dropCapMode";
  if (strcmp(key, "settings/hideGlobalStatusBar") == 0) return "settings/globalStatusBarMode";
  if (strcmp(key, "settings/hideReaderStatusBar") == 0) return "settings/readerStatusBarMode";
  return key;
}
struct State {
  std::array<std::array<uint8_t, MAX_TABS>, GROUPS> order{};
  std::array<std::array<char, KEY_SIZE>, MAX_PINS> pins{};
  uint8_t pinCount = 0;
  State() {
    for (auto& group : order)
      for (int i = 0; i < MAX_TABS; ++i) group[i] = i;
    order[0] = {0, 1, 4, 2, 3, 5, 6, 7};
    static_assert(settingstabs::TAB_COUNT == MAX_TABS, "Settings order must fit persisted tab slots");
    order[1] = settingstabs::DEFAULT_ORDER;
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
      if (strcmp(canonicalPinKey(pins[i].data()), canonicalPinKey(key)) == 0) return i;
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
// Stored slot of the pos-th shown tab. Only IDs below count are shown, so a board with
// fewer tabs than are stored (no motion sensor) steps over the ones it does not have.
inline int slotAt(int group, int pos, int count) {
  for (int i = 0, shown = 0; i < MAX_TABS; ++i) {
    if (state().order[group][i] >= count) continue;
    if (shown++ == pos) return i;
  }
  return -1;
}
inline int position(int group, int id, int count) {
  if (group < 0 || group >= GROUPS) return id;
  for (int i = 0; i < count; ++i)
    if (state().order[group][slotAt(group, i, count)] == id) return i;
  return 0;
}
inline int idAt(int group, int pos, int count) {
  if (pos < 0 || pos >= count) return 0;
  return group >= 0 && group < GROUPS ? state().order[group][slotAt(group, pos, count)] : pos;
}
inline int adjacent(int group, int id, int count, int direction) {
  return idAt(group, (position(group, id, count) + direction + count) % count, count);
}
inline bool moveTab(int group, int id, int count, int direction) {
  if (group < 0 || group >= GROUPS) return false;
  const int from = position(group, id, count), to = from + direction;
  if (to < 0 || to >= count) return false;
  auto& order = state().order[group];
  const int a = slotAt(group, from, count), b = slotAt(group, to, count);
  std::swap(order[a], order[b]);
  if (save()) return true;
  std::swap(order[a], order[b]);
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
