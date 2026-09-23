#pragma once
#include <I18n.h>

#include <optional>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/UiTabListActivity.h"
#include "activities/settings/SettingsTabs.h"

struct Rect;

// Home tabs retain logical IDs while the user changes their visual order.
// Side taps switch tabs; side holds move the active tab one position.
class HomeActivity final : public UiTabListActivity {
 public:
  // CAI_DAT chu khong phai SETTINGS: CrossPointSettings.h dinh nghia SETTINGS
  // thanh mot macro, nen Tab::SETTINGS no ra thanh mot loi bien dich kho doan.
  enum class Tab : uint8_t { RECENT, FOLDER, STATS, CAI_DAT, FAVORITES };
  static constexpr int TAB_COUNT = 5;

  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false);
  void onEnter() override;
  void restoreNavigation(const MenuNavigationState& state) override;
  void captureNavigation(MenuNavigationState& state) const override;
  void onExit() override;
  void onPause() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
#ifdef TENOR_UI_ACCEPTANCE
  void stepForTest(int direction) {
    const int count = listCount();
    if (count <= 0) {
      moveRingTo(0);
      return;
    }
    const int ring = ringPos();
    if (direction > 0) {
      moveRingTo(ring <= 0 || ring >= count ? 1 : ring + 1);
    } else if (direction < 0) {
      moveRingTo(ring <= 1 || ring > count ? count : ring - 1);
    }
  }
  // Doi thang sang the thu `index` de nghiem thu chup du anh tung man. CHI co trong ban nghiem thu USB
  // (#ifdef TENOR_UI_ACCEPTANCE) - khong vao ban phat hanh, giu lai de con chup du sau man.
  void tabForTest(int index) {
    if (index < 0 || index >= TAB_COUNT) return;
    // selectTab da tu lay RenderLock; goi thang mot lan thay vi lap stepTab de moi lan doi chi mot lan rebuild.
    if (static_cast<int>(activeTabId) != index) selectTab(static_cast<Tab>(index));
    requestUpdate();
  }
#endif

 private:
  // --- UiTabListActivity contract ---
  int tabCount() const override { return TAB_COUNT; }
  int activeTab() const override { return static_cast<int>(activeTabId); }
  const char* tabLabel(int index) const override;
  freeink::ui::BitmapRef tabIcon(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  bool supportsFavorites() const override { return activeTabId != Tab::STATS; }
  std::string favoriteKey(int row) const override;
  void favoritesChanged() override;
  bool toggleFavorite(int row) override;
  bool favoriteFileMissing = false;
  std::optional<StrId> statsResetTip;
  uint8_t statsPage = 0;
  bool statsRowsEnlarged = false;
  int statsPanelHeight() const;
  void confirmStatsReset(bool all);
  bool giuNutDiDong(int direction) override;
  // Header band, plus the cover tile on the Recent tab.
  void drawChrome() override;
  void drawFooter() override;

  int tabBarTop() const;
  int preferredTabBarHeight() const override;
  int coverTileTop() const;
  void selectTab(Tab tab);
  void rebuildRows();

  // Rows of the active tab. Rebuilt when the tab changes, never per repaint.
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> rowLabels;
  std::vector<std::string> favoriteKeys;
  std::vector<std::string> favoriteValues;
  std::vector<int> settingsGroups;
  Tab activeTabId = Tab::RECENT;

  // Noi dung goc the nho, cho the Folder. Thu muc mang dau '/' o cuoi.
  std::vector<std::string> mucTheNho;
  void docGocTheNho();

  std::vector<RecentBook> recentBooks;
  bool recentsLoaded = false;
  const HomeMenuItem initialMenuItem;
  bool cleanInitialRefresh;

  // Cover tile snapshot, so a repaint does not decode the cover again. Only the
  // tile region is kept, not the whole framebuffer.
  bool coverRendered = false;
  bool coverBufferStored = false;
  uint8_t* coverBuffer = nullptr;
  size_t coverBufferSize = 0;
  uint8_t coverBufferUiSize = 0;
  int coverRectX = 0, coverRectY = 0, coverRectW = 0, coverRectH = 0;
  // The tenor card keeps a second region, its text block, so the blank sides of the cover are not
  // held in RAM. Zero height when the snapshot is a single region.
  int textRectX = 0, textRectY = 0, textRectW = 0, textRectH = 0;
  // Recent book the snapshot shows, so stepping to another book rebuilds the card.
  int coverBufferBook = -1;
  bool storeCoverBuffer();
  bool restoreCoverBuffer();
  void freeCoverBuffer();

  // The tenor Recent tab shows one book at a time: ring position N shows recentBooks[N - 1], and
  // the front buttons walk the ring, so they step through the books.
  static constexpr size_t RECENT_LIMIT = 5;
  int shownRecent() const;
  // Saved quote each book's card shows during this visit, picked once per book (bit set in
  // cardQuotesPicked); 0 when the book has none and the card shows the page excerpt instead.
  uint64_t cardQuotes[RECENT_LIMIT] = {};
  uint8_t cardQuotesPicked = 0;
  std::string cardExcerpt(int index, bool& quoted);
  void drawRecentCard();
  void drawOtherBookRow(int shown, int ruleY, int rowY);
  const char* habitSuggestion() const;
  void loadRecentBooks();
  void onSelectBook(const std::string& path);
};
