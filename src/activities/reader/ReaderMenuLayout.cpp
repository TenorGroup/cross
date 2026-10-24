#include "ReaderMenuLayout.h"

#include <algorithm>

namespace readermenu {

void separateTextTab(std::vector<Item>& items) {
  for (auto& item : items)
    if (item.action == Action::TEXT_SETTINGS) item.tab = Tab::TEXT;
}

void buildItems(std::vector<Item>& items, const bool hasFootnotes, const bool hasBookmarks, const bool hasFrontlight,
                const bool hasTilt, const bool hasClippings) {
  items.clear();
  items.reserve(ACTION_COUNT);

  // Vi tri: ban dang o dau trong sach, va di dau. Bao duoc ca lenh luu cho dang dung.
  items.push_back({Action::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER, Tab::POSITION});
  items.push_back({Action::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT, Tab::POSITION});
  items.push_back({Action::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK, Tab::POSITION});
  if (hasBookmarks) {
    items.push_back({Action::BOOKMARKS, StrId::STR_BOOKMARKS, Tab::POSITION});
  }
  if (hasFootnotes) {
    items.push_back({Action::FOOTNOTES, StrId::STR_FOOTNOTES, Tab::POSITION});
  }

  // Text appearance lives in Text Settings. Keep the reader menu concise.
  items.push_back({Action::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS, Tab::READING});
  items.push_back({Action::NIGHT_MODE, StrId::STR_NIGHT_MODE, Tab::READING});
  // Thanh trang thai cua trinh doc: sau muc, doi ngay tai cho bang popup.
  items.push_back({Action::STATUS_BAR, StrId::STR_HIDE_READER_STATUS_BAR, Tab::READING});
  items.push_back({Action::ROTATE_SCREEN, StrId::STR_ORIENTATION, Tab::READING});
  items.push_back({Action::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN, Tab::READING});
  if (hasFrontlight) {
    items.push_back({Action::FRONTLIGHT, StrId::STR_FRONTLIGHT, Tab::READING});
  }

  items.push_back({Action::SYNC, StrId::STR_SYNC_PROGRESS, Tab::TOOLS});
  items.push_back({Action::DICTIONARY, StrId::STR_LOOKUP, Tab::TOOLS});
  items.push_back({Action::SAVE_QUOTE, StrId::STR_QUOTES_SAVE_ACTION, Tab::TOOLS});
  // Right under Save quotation: the quotes of this book are where a saved one is read again,
  // trimmed or reselected on its page.
  items.push_back({Action::QUOTES_OF_BOOK, StrId::STR_QUOTES_OF_BOOK, Tab::TOOLS});
  // The upstream clippings (D7) beside the quotes: select text and keep it. The rows above keep their places.
  items.push_back({Action::SAVE_CLIPPING, StrId::STR_SAVE_CLIPPING, Tab::TOOLS});
  items.push_back({Action::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON, Tab::TOOLS});
  items.push_back({Action::DISPLAY_QR, StrId::STR_DISPLAY_QR, Tab::TOOLS});
  items.push_back({Action::DELETE_CACHE, StrId::STR_DELETE_CACHE, Tab::TOOLS});
  // Ve man chinh goi onGoHome() va ve MAN CHINH, khac han nut Quay lai von ve
  // lai TRANG SACH, nen no khong thua.
  items.push_back({Action::GO_HOME, StrId::STR_GO_HOME_BUTTON, Tab::TOOLS});
  items.push_back({Action::BLUETOOTH, StrId::STR_BLE_PAGE_TURNER, Tab::TOOLS});
  // Quick actions a reader may want one hold away: pinnable into Favorites.
  items.push_back({Action::FILE_TRANSFER, StrId::STR_FILE_TRANSFER, Tab::TOOLS});
  if (hasTilt) {
    items.push_back({Action::TILT_PAGE_TURN, StrId::STR_TILT_PAGE_TURN, Tab::TOOLS});
  }
  // The clippings of the upstream reader (D7): the list of a book that has some is conditional, so it closes the tab.
  if (hasClippings) {
    items.push_back({Action::VIEW_CLIPPINGS, StrId::STR_VIEW_CLIPPINGS, Tab::TOOLS});
  }
}

void buildMoreItems(std::vector<Item>& items, const bool hasFootnotes, const bool hasBookmarks,
                    const bool hasFrontlight, const bool hasSearch, const bool hasTilt, const bool hasClippings) {
  buildItems(items, hasFootnotes, hasBookmarks, hasFrontlight, hasTilt, hasClippings);
  items.erase(std::remove_if(items.begin(), items.end(),
                             [](const auto& item) {
                               return item.action == Action::SELECT_CHAPTER || item.action == Action::TEXT_SETTINGS;
                             }),
              items.end());
  static constexpr Action order[] = {Action::FOOTNOTES,     Action::BOOKMARKS,      Action::TOGGLE_BOOKMARK,
                                     Action::NIGHT_MODE,    Action::STATUS_BAR,     Action::FRONTLIGHT,
                                     Action::DICTIONARY,    Action::ROTATE_SCREEN,  Action::AUTO_PAGE_TURN,
                                     Action::GO_TO_PERCENT, Action::SCREENSHOT,     Action::DISPLAY_QR,
                                     Action::GO_HOME,       Action::SYNC,           Action::DELETE_CACHE,
                                     Action::SAVE_QUOTE,    Action::QUOTES_OF_BOOK, Action::SAVE_CLIPPING,
                                     Action::BLUETOOTH,     Action::FILE_TRANSFER,  Action::TILT_PAGE_TURN,
                                     Action::VIEW_CLIPPINGS};
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return std::find(std::begin(order), std::end(order), a.action) <
           std::find(std::begin(order), std::end(order), b.action);
  });
  if (hasSearch) items.insert(items.begin(), {Action::BOOK_SEARCH, StrId::STR_BOOK_SEARCH, Tab::TOOLS});
}

int rowsOfTab(const std::vector<Item>& all, const Tab tab, uint8_t* out, const int max) {
  int n = 0;
  for (size_t i = 0; i < all.size() && n < max; i++) {
    if (all[i].tab == tab) out[n++] = static_cast<uint8_t>(i);
  }
  return n;
}

int rowsOfFavorites(const std::vector<Item>& all, const Action* favorites, const int count, uint8_t* out,
                    const int max) {
  int n = 0;
  for (int f = 0; f < count && n < max; f++) {
    for (size_t i = 0; i < all.size(); i++) {
      if (all[i].action == favorites[f]) {
        out[n++] = static_cast<uint8_t>(i);
        break;
      }
    }
  }
  return n;
}

}  // namespace readermenu
