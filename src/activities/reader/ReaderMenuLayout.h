#pragma once
#include <I18n.h>

#include <cstdint>
#include <vector>

namespace readermenu {

// Thu tu cac gia tri nay di qua MenuResult duoi dang int, nen muc moi phai
// THEM VAO CUOI. Chen vao giua la chay nham lenh.
enum class Action {
  SELECT_CHAPTER,
  FOOTNOTES,
  TEXT_SETTINGS,
  NIGHT_MODE,
  STATUS_BAR,  // doi muc thanh trang thai trong trinh doc
  FRONTLIGHT,
  GO_TO_PERCENT,
  AUTO_PAGE_TURN,
  ROTATE_SCREEN,
  BOOKMARKS,
  TOGGLE_BOOKMARK,
  SCREENSHOT,
  DISPLAY_QR,
  GO_HOME,
  SYNC,
  DELETE_CACHE,
  DICTIONARY,
  FONT_SIZE,    // mo TextSettings o the Co chu
  FONT_FAMILY,  // mo TextSettings o the Font chu
  SAVE_QUOTE
};

// So lenh dang co. Dung de loc mot o rac doc len tu settings.json: mot so vuot tam nghia
// la ban ghi cu tro toi mot lenh khong con ton tai.
inline constexpr int ACTION_COUNT = static_cast<int>(Action::SAVE_QUOTE) + 1;

enum class Tab : uint8_t { FAVORITES, POSITION, READING, TOOLS };
inline constexpr int TAB_COUNT = 4;

struct Item {
  Action action;
  StrId labelId;
  Tab tab;
};

// Muc co dieu kien (chu thich, dau trang, den nen) xep CUOI tab cua no, de cac
// muc luon hien giu nguyen so thu tu du cuon sach nay co va cuon kia khong.
void buildItems(std::vector<Item>& items, bool hasFootnotes, bool hasBookmarks, bool hasFrontlight);

// Toolbar More keeps its original order independently of the tab layout.
void buildMoreItems(std::vector<Item>& items, bool hasFootnotes, bool hasBookmarks, bool hasFrontlight);

// Hai ham duoi ghi CHI SO trong `all` vao `out`, toi da `max` chi so, va tra ve
// so dong ghi duoc. Tra chi so chu khong tra ban sao vi hai le: man hinh can chi
// so de tra nguoc ve muc, va khong ham nao cap phat bo nho.

// Cac dong cua mot tab thuong, giu nguyen thu tu trong danh sach day du.
int rowsOfTab(const std::vector<Item>& all, Tab tab, uint8_t* out, int max);

// Cac dong cua tab Yeu thich: dung nhung muc co trong `favorites`, theo DUNG THU TU
// nguoi dung da xep, chu khong theo thu tu cua danh sach day du. Muc da chon nhung
// khong ton tai o cuon sach nay (vi du Den nen tren X3) bi bo qua.
int rowsOfFavorites(const std::vector<Item>& all, const Action* favorites, int count, uint8_t* out, int max);

// Danh sach yeu thich mac dinh khi nguoi dung chua tu xep: dong bo tenor/kosync.
inline constexpr Action DEFAULT_FAVORITES[] = {Action::SYNC};

// So muc ghim toi da. Tran ton tai vi danh sach ghim nam trong settings.json va vi
// mot tab dai qua thi mat chinh cai loi cua no.
inline constexpr int TOI_DA_GHIM = 8;

// --- ghim va xep --------------------------------------------------------------------
//
// Ba ham duoi la LUAT cua tab Yeu thich. Chung thuan, khong dung toi man hinh hay the
// nho, nen bai kiem giu duoc chung (test/reader_menu_tabs).

// Muc nay dang duoc ghim chua.
bool daGhim(const std::vector<Action>& ghim, Action action);

// Giu nut Chon tren mot dong la goi ham nay: dang ghim thi go ra, chua ghim thi them
// vao CUOI. Tra ve true neu danh sach doi; false nghia la ghim day roi.
bool doiGhim(std::vector<Action>& ghim, Action action);

// Giu nut Len hoac Xuong trong tab Yeu thich la goi ham nay: day muc o `viTri` di mot
// bac theo `huong` (-1 len, 1 xuong). QUAY VONG, vi ca may quay vong, va vi nho vay dua
// mot muc tu day len dau chi ton mot nhip. Tra ve vi tri moi cua muc do.
int dayBac(std::vector<Action>& ghim, int viTri, int huong);

}  // namespace readermenu
