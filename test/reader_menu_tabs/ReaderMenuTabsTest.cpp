#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "activities/reader/ReaderMenuLayout.h"

namespace {

using MenuAction = readermenu::Action;
using MenuItem = readermenu::Item;
using MenuTab = readermenu::Tab;

// Bon tab CO SO HUU dong. Tab Yeu thich khong so huu gi, no tro toi bon tab nay.
constexpr MenuTab kTabsThuong[] = {MenuTab::POSITION, MenuTab::READING, MenuTab::TOOLS};

std::vector<MenuItem> menu(const bool footnotes, const bool bookmarks, const bool frontlight) {
  std::vector<MenuItem> items;
  readermenu::buildItems(items, footnotes, bookmarks, frontlight);
  return items;
}

std::vector<MenuAction> rows(const std::vector<MenuItem>& all, const MenuTab tab) {
  uint8_t idx[32];
  const int n = readermenu::rowsOfTab(all, tab, idx, 32);
  std::vector<MenuAction> actions;
  for (int i = 0; i < n; i++) actions.push_back(all[idx[i]].action);
  return actions;
}

std::vector<MenuAction> yeuThich(const std::vector<MenuItem>& all, const MenuAction* xep, const int count) {
  uint8_t idx[32];
  const int n = readermenu::rowsOfFavorites(all, xep, count, idx, 32);
  std::vector<MenuAction> actions;
  for (int i = 0; i < n; i++) actions.push_back(all[idx[i]].action);
  return actions;
}

// Cai day du: sach co chu thich, co dau trang, may co den nen.
std::vector<MenuItem> menuDayDu() { return menu(true, true, true); }

TEST(ReaderMenuTabs, BonTab) { EXPECT_EQ(readermenu::TAB_COUNT, 4); }

// Dong dong bo phai co nha that, khong duoc bien mat cung voi cai tab cu.
TEST(ReaderMenuTabs, DongDongBoVeTabCongCu) {
  const auto all = menuDayDu();
  const auto congCu = rows(all, MenuTab::TOOLS);
  ASSERT_FALSE(congCu.empty());
  EXPECT_EQ(congCu.front(), MenuAction::SYNC) << "dong dong bo phai dung DAU tab Cong cu";
}

// Moi muc phai co dung mot nha. Mot muc khong thuoc tab nao la mot muc BIEN MAT
// khoi may sau khi chia tab, va no bien mat im lang.
TEST(ReaderMenuTabs, MoiMucCoDungMotNha) {
  const auto all = menuDayDu();
  ASSERT_FALSE(all.empty());

  size_t tongSoDong = 0;
  std::set<MenuAction> daThay;
  for (const auto tab : kTabsThuong) {
    for (const auto action : rows(all, tab)) {
      EXPECT_TRUE(daThay.insert(action).second) << "mot muc nam o hai tab cung luc";
      tongSoDong++;
    }
  }
  EXPECT_EQ(tongSoDong, all.size()) << "tong so dong bon tab phai bang so muc cua menu";
}

// Tab Yeu thich KHONG duoc so huu muc nao, vi no chi tro toi muc dang song o tab khac.
// So huu thi muc do bien mat khoi tab that cua no.
TEST(ReaderMenuTabs, TabYeuThichKhongSoHuuMucNao) { EXPECT_TRUE(rows(menuDayDu(), MenuTab::FAVORITES).empty()); }

TEST(ReaderMenuTabs, TabThuongNaoCungCoDong) {
  const auto all = menuDayDu();
  for (const auto tab : kTabsThuong) {
    EXPECT_FALSE(rows(all, tab).empty()) << "tab so " << static_cast<int>(tab) << " khong co dong nao";
  }
}

TEST(ReaderMenuTabs, TextSettingsOwnsFontAndSize) {
  const auto all = menuDayDu();
  const auto reading = rows(all, MenuTab::READING);
  ASSERT_FALSE(reading.empty());
  EXPECT_EQ(reading[0], MenuAction::TEXT_SETTINGS);
  for (const auto& item : all) {
    EXPECT_NE(item.action, MenuAction::FONT_SIZE);
    EXPECT_NE(item.action, MenuAction::FONT_FAMILY);
  }
  const MenuAction oldPins[] = {MenuAction::FONT_SIZE, MenuAction::TEXT_SETTINGS, MenuAction::FONT_FAMILY};
  uint8_t out[3]{};
  ASSERT_EQ(readermenu::rowsOfFavorites(all, oldPins, 3, out, 3), 1);
  EXPECT_EQ(all[out[0]].action, MenuAction::TEXT_SETTINGS);
}

// LUAT, cho de vo nhat. Muc luon hien phai giu NGUYEN so thu tu trong tab cua no,
// bat ke sach co chu thich hay khong, co dau trang hay khong, may co den nen hay khong.
TEST(ReaderMenuTabs, MucCoDieuKienXepCuoiTab) {
  const auto day = menuDayDu();
  const auto troi = menu(false, false, false);

  for (const auto tab : kTabsThuong) {
    const auto dongDay = rows(day, tab);
    const auto dongTroi = rows(troi, tab);
    ASSERT_LE(dongTroi.size(), dongDay.size()) << "bo dieu kien di ma tab lai DAI ra";
    for (size_t i = 0; i < dongTroi.size(); i++) {
      EXPECT_EQ(dongTroi[i], dongDay[i]) << "tab so " << static_cast<int>(tab) << ", dong thu " << i
                                         << " tro sang mot lenh khac khi sach khong co muc dieu kien";
    }
  }
}

// Yeu thich giu DUNG THU TU nguoi dung xep, khong theo thu tu danh sach day du.
TEST(ReaderMenuTabs, YeuThichGiuThuTuNguoiDungXep) {
  const auto all = menuDayDu();
  const MenuAction xep[] = {MenuAction::DELETE_CACHE, MenuAction::SYNC, MenuAction::SELECT_CHAPTER};
  const auto out = yeuThich(all, xep, 3);

  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], MenuAction::DELETE_CACHE);
  EXPECT_EQ(out[1], MenuAction::SYNC);
  EXPECT_EQ(out[2], MenuAction::SELECT_CHAPTER);
}

// Muc da chon nhung cuon sach nay khong co (hoac may khong co den nen) thi bo qua,
// chu khong duoc de lai mot dong rong hay mot dong tro sai.
TEST(ReaderMenuTabs, YeuThichBoQuaMucKhongTonTai) {
  const auto khongDenNen = menu(false, false, false);
  const MenuAction xep[] = {MenuAction::FRONTLIGHT, MenuAction::SYNC};
  const auto out = yeuThich(khongDenNen, xep, 2);

  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], MenuAction::SYNC);
}

TEST(ReaderMenuTabs, MacDinhYeuThichLaDongBo) {
  ASSERT_EQ(std::size(readermenu::DEFAULT_FAVORITES), 1u);
  EXPECT_EQ(readermenu::DEFAULT_FAVORITES[0], MenuAction::SYNC);
}

// Ve man chinh o lai menu. Do 14/09: nut Quay lai goi closeCancelled() va ve lai
// TRANG SACH, con muc nay goi onGoHome() va ve MAN CHINH. Hai dich khac nhau.
TEST(ReaderMenuTabs, VeManChinhVanConTrongMenu) {
  const auto all = menuDayDu();
  bool co = false;
  for (const auto& item : all) {
    if (item.action == MenuAction::GO_HOME) co = true;
  }
  EXPECT_TRUE(co) << "muc Ve man chinh bi go mat";
}


TEST(GhimYeuThich, ChuaGhimThiGhimVaoCUOI) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC};
  EXPECT_FALSE(readermenu::daGhim(ghim, MenuAction::DICTIONARY));
  EXPECT_TRUE(readermenu::doiGhim(ghim, MenuAction::DICTIONARY));
  ASSERT_EQ(ghim.size(), 2u);
  EXPECT_EQ(ghim[1], MenuAction::DICTIONARY) << "muc moi phai xuong cuoi, khong chen vao giua";
  EXPECT_TRUE(readermenu::daGhim(ghim, MenuAction::DICTIONARY));
}

TEST(GhimYeuThich, GhimLanNuaThiGoRa) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC, MenuAction::DICTIONARY};
  EXPECT_TRUE(readermenu::doiGhim(ghim, MenuAction::SYNC));
  ASSERT_EQ(ghim.size(), 1u);
  EXPECT_EQ(ghim[0], MenuAction::DICTIONARY) << "go muc dau thi muc sau phai don len";
  EXPECT_FALSE(readermenu::daGhim(ghim, MenuAction::SYNC));
}

TEST(GhimYeuThich, DayRoiThiKhongNhanThem) {
  std::vector<MenuAction> ghim;
  const MenuAction du[] = {MenuAction::SYNC,       MenuAction::DICTIONARY,   MenuAction::SCREENSHOT,
                           MenuAction::DISPLAY_QR, MenuAction::DELETE_CACHE, MenuAction::NIGHT_MODE,
                           MenuAction::FONT_SIZE,  MenuAction::FONT_FAMILY};
  for (const auto a : du) ASSERT_TRUE(readermenu::doiGhim(ghim, a));
  ASSERT_EQ(static_cast<int>(ghim.size()), readermenu::TOI_DA_GHIM);

  EXPECT_FALSE(readermenu::doiGhim(ghim, MenuAction::BOOKMARKS)) << "day roi thi phai tu choi";
  EXPECT_EQ(static_cast<int>(ghim.size()), readermenu::TOI_DA_GHIM);
  // Nhung go ra thi van phai duoc, ke ca khi dang day.
  EXPECT_TRUE(readermenu::doiGhim(ghim, MenuAction::SYNC));
}

TEST(XepYeuThich, DayMotBacGiuNguyenCacMucKhac) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC, MenuAction::DICTIONARY, MenuAction::SCREENSHOT};
  EXPECT_EQ(readermenu::dayBac(ghim, 2, -1), 1) << "day len mot bac thi vi tri moi la 1";
  ASSERT_EQ(ghim.size(), 3u);
  EXPECT_EQ(ghim[0], MenuAction::SYNC);
  EXPECT_EQ(ghim[1], MenuAction::SCREENSHOT);
  EXPECT_EQ(ghim[2], MenuAction::DICTIONARY);
}

// Quay vong, giong moi danh sach khac cua may: tu day len dau ton dung mot nhip.
TEST(XepYeuThich, DayQuaDauThiVongXuongCuoi) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC, MenuAction::DICTIONARY, MenuAction::SCREENSHOT};
  EXPECT_EQ(readermenu::dayBac(ghim, 0, -1), 2);
  EXPECT_EQ(ghim[2], MenuAction::SYNC);
  EXPECT_EQ(ghim[0], MenuAction::DICTIONARY);

  std::vector<MenuAction> ghim2 = {MenuAction::SYNC, MenuAction::DICTIONARY, MenuAction::SCREENSHOT};
  EXPECT_EQ(readermenu::dayBac(ghim2, 2, 1), 0);
  EXPECT_EQ(ghim2[0], MenuAction::SCREENSHOT);
}

TEST(XepYeuThich, ViTriNgoaiPhamViThiKhongLamGi) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC, MenuAction::DICTIONARY};
  const auto truoc = ghim;
  readermenu::dayBac(ghim, -1, 1);
  readermenu::dayBac(ghim, 5, -1);
  EXPECT_EQ(ghim, truoc);
}

TEST(XepYeuThich, MotMucThiDayDiDauCungDung) {
  std::vector<MenuAction> ghim = {MenuAction::SYNC};
  EXPECT_EQ(readermenu::dayBac(ghim, 0, 1), 0);
  EXPECT_EQ(readermenu::dayBac(ghim, 0, -1), 0);
  ASSERT_EQ(ghim.size(), 1u);
}

TEST(ToolbarMore, KeepsLegacyOrderForEveryOptionalItemCombination) {
  const MenuAction expected[] = {MenuAction::FOOTNOTES,     MenuAction::BOOKMARKS,      MenuAction::TOGGLE_BOOKMARK,
                                 MenuAction::NIGHT_MODE,    MenuAction::STATUS_BAR,     MenuAction::FRONTLIGHT,
                                 MenuAction::DICTIONARY,    MenuAction::ROTATE_SCREEN,  MenuAction::AUTO_PAGE_TURN,
                                 MenuAction::GO_TO_PERCENT, MenuAction::SCREENSHOT,     MenuAction::DISPLAY_QR,
                                 MenuAction::GO_HOME,       MenuAction::SYNC,           MenuAction::DELETE_CACHE,
                                 MenuAction::SAVE_QUOTE};
  for (int flags = 0; flags < 8; ++flags) {
    std::vector<readermenu::Item> items;
    readermenu::buildMoreItems(items, flags & 1, flags & 2, flags & 4);
    size_t row = 0;
    for (const auto action : expected) {
      if ((action == MenuAction::FOOTNOTES && !(flags & 1)) || (action == MenuAction::BOOKMARKS && !(flags & 2)) ||
          (action == MenuAction::FRONTLIGHT && !(flags & 4)))
        continue;
      ASSERT_LT(row, items.size());
      EXPECT_EQ(items[row++].action, action) << "optional flags=" << flags;
    }
    EXPECT_EQ(items.size(), row);
  }
}

}  // namespace
