#pragma once

#include <vector>

#include "activities/settings/SettingsTabs.h"

namespace homesettings {

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
constexpr bool kTitled = true;
#else
constexpr bool kTitled = false;
#endif

// The Settings page of Home, one list on every device: row 0 is Connections, then `groups` (the groups this board
// shows, in the stored order), then About & updates. On the X4 Pro the default order falls under three titles
// (founder 06/10 and 20/10/2026): Reading opens with Connections, System with System, Other with Sleep. An order the
// user chose no longer matches those groups, so its rows stay untitled. The X3 and X4 Classic list the same rows
// untitled and evenly spaced, with no gap between groups (founder 20/10/2026).
inline StrId heading(const std::vector<int>& groups, const int row) {
  using Tab = settingstabs::Tab;
  if (!kTitled || row < 0 || row > static_cast<int>(groups.size())) return StrId::STR_NONE_OPT;
  size_t next = 0;  // the stored order against the default, without the tabs this board does not show
  for (const int id : settingstabs::DEFAULT_ORDER)
    if (id < static_cast<int>(groups.size()) && groups[next++] != id) return StrId::STR_NONE_OPT;
  if (row == 0) return StrId::STR_READER_TAB_READING;
  if (groups[row - 1] == static_cast<int>(Tab::SYSTEM)) return StrId::STR_CAT_SYSTEM;
  if (groups[row - 1] == static_cast<int>(Tab::SLEEP)) return StrId::STR_CAT_OTHER;
  return StrId::STR_NONE_OPT;
}

}  // namespace homesettings
