#include "ReaderMenuLayout.h"

#include <algorithm>

namespace readermenu {

void buildItems(std::vector<Item>& items, const bool hasFootnotes, const bool hasBookmarks, const bool hasFrontlight) {
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
  items.push_back({Action::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON, Tab::TOOLS});
  items.push_back({Action::DISPLAY_QR, StrId::STR_DISPLAY_QR, Tab::TOOLS});
  items.push_back({Action::DELETE_CACHE, StrId::STR_DELETE_CACHE, Tab::TOOLS});
  // Ve man chinh goi onGoHome() va ve MAN CHINH, khac han nut Quay lai von ve
  // lai TRANG SACH, nen no khong thua.
  items.push_back({Action::GO_HOME, StrId::STR_GO_HOME_BUTTON, Tab::TOOLS});
}

void buildMoreItems(std::vector<Item>& items, const bool hasFootnotes, const bool hasBookmarks,
                    const bool hasFrontlight) {
  buildItems(items, hasFootnotes, hasBookmarks, hasFrontlight);
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
                                     Action::SAVE_QUOTE};
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return std::find(std::begin(order), std::end(order), a.action) <
           std::find(std::begin(order), std::end(order), b.action);
  });
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

bool daGhim(const std::vector<Action>& ghim, const Action action) {
  for (const auto a : ghim) {
    if (a == action) return true;
  }
  return false;
}

bool doiGhim(std::vector<Action>& ghim, const Action action) {
  for (size_t i = 0; i < ghim.size(); i++) {
    if (ghim[i] != action) continue;
    ghim.erase(ghim.begin() + static_cast<long>(i));
    return true;
  }
  // Go ra thi luc nao cung duoc, ke ca khi day; chi viec THEM moi vuong tran.
  if (static_cast<int>(ghim.size()) >= TOI_DA_GHIM) return false;
  ghim.push_back(action);
  return true;
}

int dayBac(std::vector<Action>& ghim, const int viTri, const int huong) {
  const int n = static_cast<int>(ghim.size());
  if (viTri < 0 || viTri >= n || n <= 1 || huong == 0) return viTri;

  // Nho ra roi cam vao, chu khong doi cho hai muc: doi cho thi khi quay vong, muc nam
  // giua cung bi hat di, va thu tu nguoi ta xep vo.
  int moi = (viTri + huong) % n;
  if (moi < 0) moi += n;
  const Action a = ghim[static_cast<size_t>(viTri)];
  ghim.erase(ghim.begin() + viTri);
  ghim.insert(ghim.begin() + moi, a);
  return moi;
}

}  // namespace readermenu
