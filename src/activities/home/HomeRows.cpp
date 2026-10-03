#include "HomeRows.h"

#include <Utf8.h>

#include <algorithm>

#include "FileFavorites.h"
#include "MenuCustomization.h"
#include "MenuFavorites.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"

namespace homerows {

std::vector<RecentBook> recent(const size_t limit) {
  std::vector<RecentBook> out;
  const auto& books = RECENT_BOOKS.getBooks();
  out.reserve(std::min(books.size(), limit));
  for (const RecentBook& book : books) {
    if (RecentBooksStore::isMissing(book)) continue;
    out.push_back(book);
    if (out.size() == limit) break;
  }
  return out;
}

Favorites favorites() {
  Favorites out;
  for (int i = 0; i < menucustom::state().pinCount; ++i) {
    const std::string key = menucustom::state().pins[i].data();
    if (filefavorites::isFileKey(key)) {
      const auto path = filefavorites::pathFor(key);
      out.keys.push_back(key);
      out.values.emplace_back(key.rfind("folder/", 0) == 0 ? tr(STR_HOME_TAB_FOLDER) : "");
      out.labels.push_back(path.empty() ? tr(STR_DICT_NOT_FOUND) : utf8ComposeNfc(path.substr(path.find_last_of('/') + 1)));
      continue;
    }
    const auto name = menufavorites::label(key);
    if (name == StrId::STR_NONE_OPT) continue;
    out.keys.push_back(key);
    out.values.push_back(menufavorites::value(key, &sdFontSystem.registry()));
    out.labels.emplace_back(I18N.get(name));
  }
  return out;
}

SettingsGroups settingsGroups() {
  SettingsGroups out;
  const int groups = deviceSettingsTabCount();
  for (int i = 0; i < groups; ++i) {
    const int id = menucustom::idAt(1, i, groups);
    out.ids.push_back(id);
    out.labels.emplace_back(I18N.get(settingstabs::tenThe(static_cast<settingstabs::Tab>(id))));
  }
  return out;
}

}  // namespace homerows
