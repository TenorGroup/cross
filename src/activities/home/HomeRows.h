#pragma once
// What the Home pages list, as data. Both shells (tenor/cross Home, tenor/ugly notebook) read their rows
// from here, so the five pages show the same books, pins and groups whichever shell draws them.
#include <I18n.h>

#include <string>
#include <vector>

#include "RecentBooksStore.h"

namespace homerows {

// The five pages, numbered as HomeActivity::Tab: the ids the page order (menucustom group 0) stores.
enum class Page : uint8_t { Recent, Folder, Stats, Settings, Favorites };
inline constexpr int PAGE_COUNT = 5;
// The name each page goes by, in Page order.
inline constexpr StrId PAGE_TITLES[PAGE_COUNT] = {StrId::STR_HOME_TAB_RECENT, StrId::STR_HOME_TAB_FOLDER, StrId::STR_HOME_TAB_STATS,
                                                 StrId::STR_SETTINGS_TITLE, StrId::STR_READER_TAB_FAVORITES};

// Books that are still on the card, newest first, at most `limit`.
std::vector<RecentBook> recent(size_t limit);

// The Favorites page: pins in their saved order. keys, values and labels are parallel.
struct Favorites {
  std::vector<std::string> keys, values, labels;
};
Favorites favorites();

// The Settings page below "Send files": one row per group this board shows, in the user's order.
struct SettingsGroups {
  std::vector<int> ids;
  std::vector<std::string> labels;
};
SettingsGroups settingsGroups();

// The Stats page rows, in order: habits, by book, month, quotes, reset all, reset habits.
inline constexpr StrId STATS_ROWS[] = {StrId::STR_READING_HABITS, StrId::STR_STATS_BY_BOOK, StrId::STR_STATS_MONTH,
                                       StrId::STR_QUOTES,         StrId::STR_STATS_RESET_ALL,
                                       StrId::STR_STATS_RESET_HABITS};
inline constexpr int STATS_ROW_COUNT = 6;

}  // namespace homerows
