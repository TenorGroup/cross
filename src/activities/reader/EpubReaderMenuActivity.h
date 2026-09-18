#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "activities/reader/ReaderMenuLayout.h"
#include "components/OptionPopup.h"

class EpubReaderMenuActivity final : public UiTabListActivity {
 public:
  // The grouping itself lives in ReaderMenuLayout, which stays clear of device
  // headers so it can be exercised on the host. These aliases keep the names
  // EpubReaderActivity already switches on.
  using MenuAction = readermenu::Action;
  using MenuItem = readermenu::Item;
  using MenuTab = readermenu::Tab;

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  // Row storage for the active tab. menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set by rebuildRows() when the tab changes; buildScreen() only refreshes
  // rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 20;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  // Row within the active tab -> index into menuItems.
  uint8_t rowToItem[MAX_MENU_ITEMS]{};
  int rowCount = 0;
  void rebuildRows();

  // --- UiTabListActivity contract ---
  int tabCount() const override { return readermenu::TAB_COUNT; }
  int activeTab() const override { return static_cast<int>(activeTabId); }
  const char* tabLabel(int index) const override;
  freeink::ui::BitmapRef tabIcon(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, tab steps) falls through to the base handler.
  bool handleButtons() override;
  bool rowIsPinned(int row) const override;
  // Giu cap nut di-dong: trong tab Yeu thich thi day dong dang chon di mot bac.
  bool giuNutDiDong(int huong) override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();
  void selectTab(MenuTab tab);
  // Dong menu voi viec da chon; coChu va hoFont mang lua chon tai cho (0 va -1 nghia la mo man).
  void dongVoi(MenuAction viec, uint8_t coChu, int8_t hoFont);
  void chonCoChu();
  void chonHoFont();

  // --- tab Yeu thich ---------------------------------------------------------------
  // Giu nut Chon tren mot dong bat ky la ghim dong do vao Yeu thich, giu lan nua la go
  // ra. Chon nhip GIU vi no dang trong o man nay, con them mot man hay mot the moi thi
  // ton nhip bam, ma dieu mot cua tenor/cross la it nut nhat.
  bool doiGhimDongDangChon();
  // Trong tab Yeu thich, giu nut Len hoac Xuong la day dong dang chon di mot bac.
  bool xepLaiDongDangChon(int huong);
  // Ghi danh sach ghim xuong settings.json.
  void luuGhim();
  // Doc danh sach ghim tu settings.json, hoac lay ban mac dinh khi nguoi doc chua ghim gi.
  void napGhim();

  // Nhip giu, tinh bang mili giay. Bang nguong cua trinh duyet tep, de ca may mot tay.
  static constexpr unsigned long GIU_MS = 1000;

  // Every row of every tab, built once in the constructor.
  std::vector<MenuItem> menuItems;
  // The menu opens on Favorites: it is the one tab whose contents the reader
  // chose, so it is the cheapest place to land.
  MenuTab activeTabId = MenuTab::FAVORITES;
  // Actions the reader pinned to the Favorites tab, in their order. Seeded with
  // readermenu::DEFAULT_FAVORITES until the reader pins their own.
  std::vector<MenuAction> favorites;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
