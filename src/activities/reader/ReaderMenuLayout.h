#pragma once
#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
  SAVE_QUOTE,
  BLUETOOTH,
  QUOTES_OF_BOOK,  // mo man Trich dan cua dung cuon dang doc
  FILE_TRANSFER,   // leaves the book for the file transfer screen
  TILT_PAGE_TURN   // toggles tilt page turn in place; boards with an IMU only
};

// So lenh dang co. Dung de loc mot o rac doc len tu settings.json: mot so vuot tam nghia
// la ban ghi cu tro toi mot lenh khong con ton tai.
inline constexpr int ACTION_COUNT = static_cast<int>(Action::TILT_PAGE_TURN) + 1;

enum class Tab : uint8_t { FAVORITES, POSITION, READING, TOOLS };
inline constexpr int TAB_COUNT = 4;

// The tools of the toolbar reader menu (its tabs on the button boards, the X4 Pro's reader bar), in the order they
// stand from left to right (founder 07/10: Favorites first). The one place that says it: a tool's value is its
// place on the bar, and every icon, name and panel is looked up from the tool, never from a place.
enum class Tool : uint8_t { FAVORITES, CONTENTS, TEXT, MORE };
inline constexpr int TOOL_COUNT = 4;
constexpr StrId toolName(const Tool tool) {
  switch (tool) {
    case Tool::FAVORITES: return StrId::STR_READER_TAB_FAVORITES;
    case Tool::CONTENTS: return StrId::STR_TOOL_CONTENTS;
    case Tool::TEXT: return StrId::STR_TOOL_TEXT;
    case Tool::MORE: break;
  }
  return StrId::STR_TOOL_MORE;
}

struct Item {
  Action action;
  StrId labelId;
  Tab tab;
};

// Muc co dieu kien (chu thich, dau trang, den nen) xep CUOI tab cua no, de cac
// muc luon hien giu nguyen so thu tu du cuon sach nay co va cuon kia khong.
void buildItems(std::vector<Item>& items, bool hasFootnotes, bool hasBookmarks, bool hasFrontlight,
                bool hasTilt = false);

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

// Ten sau muc thanh trang thai khi doc, theo dung thu tu CrossPointSettings::READER_STATUS_BAR_MODE (gia tri
// luu). Mot danh sach cho popup cua menu danh sach va danh sach gia tri cua menu thanh cong cu.
inline constexpr StrId STATUS_BAR_MODE_LABELS[] = {
    StrId::STR_STATE_OFF,                   StrId::STR_STATUS_BAR_CLOCK_BATTERY, StrId::STR_STATUS_BAR_DEFAULT,
    StrId::STR_STATUS_BAR_CHAPTER_PROGRESS, StrId::STR_STATUS_BAR_CHAPTER_CLOCK, StrId::STR_STATUS_BAR_CHAPTER_BATTERY,
    StrId::STR_STATUS_BAR_CLOCK_CHAPTER_PROGRESS, StrId::STR_STATUS_BAR_BOOK_DETAILS};

// Dong cua lenh nay mo mot danh sach hay mot man khac (co mui ten ">" mo o cuoi dong, nhu Cai dat), hay lam
// ngay tai cho (bat tat, chup, dat dau trang: khong mui ten).
constexpr bool rowOpens(const Action action) {
  return action != Action::TOGGLE_BOOKMARK && action != Action::NIGHT_MODE && action != Action::FRONTLIGHT &&
         action != Action::SCREENSHOT && action != Action::TILT_PAGE_TURN;
}

// Danh sach yeu thich mac dinh khi nguoi dung chua tu xep: dong bo tenor/kosync.
inline constexpr Action DEFAULT_FAVORITES[] = {Action::SYNC};

// So muc ghim toi da. Tran ton tai vi danh sach ghim nam trong settings.json va vi
// mot tab dai qua thi mat chinh cai loi cua no.
inline constexpr int TOI_DA_GHIM = 8;

// Nhip giu nut Chon de ghim hay go mot dong, mili giay. Mot nguong cho ca hai menu doc tren may nut
// (menu danh sach va menu thanh cong cu), bang nguong cua trinh duyet tep.
inline constexpr unsigned long GIU_GHIM_MS = 1000;

// --- ghim va xep --------------------------------------------------------------------
//
// Ba ham duoi la LUAT cua tab Yeu thich. Chung thuan, khong dung toi man hinh hay the
// nho, nen bai kiem giu duoc chung (test/reader_menu_tabs).

// Muc nay dang duoc ghim chua.
template <class T>
bool daGhim(const std::vector<T>& ghim, const T muc) {
  for (const auto m : ghim)
    if (m == muc) return true;
  return false;
}

// Giu nut Chon tren mot dong la goi ham nay: dang ghim thi go ra, chua ghim thi them
// vao CUOI. Tra ve true neu danh sach doi; false nghia la ghim day roi.
// Mot luat cho ca hai menu: X3 ghim lenh (Action), X4 Pro ghim ma ghim (PIN_TEXT).
template <class T>
bool doiGhim(std::vector<T>& ghim, const T muc) {
  for (size_t i = 0; i < ghim.size(); i++) {
    if (ghim[i] != muc) continue;
    ghim.erase(ghim.begin() + static_cast<long>(i));
    return true;
  }
  // Go ra thi luc nao cung duoc, ke ca khi day; chi viec THEM moi vuong tran.
  if (static_cast<int>(ghim.size()) >= TOI_DA_GHIM) return false;
  ghim.push_back(muc);
  return true;
}

// X4 Pro (founder 06/10): the reader menu's Favorites pins actions and text settings. A pin is one byte in
// readerFavorites: an Action (below ACTION_COUNT), or PIN_TEXT | the text setting's place in TEXT_KEYS.
// settings.json keeps an Action as its number, as every earlier file does, and a text setting as
// "text/<key>", so an old file loads unchanged.
inline constexpr uint8_t PIN_TEXT = 0x80;
inline constexpr const char* TEXT_KEYS[] = {
    "fontFamily",    "fontSize",        "lineSpacing",       "paragraphAlignment", "dropCapMode",
    "letterSpacing", "wordSpacing",     "extraParagraphSpacing", "screenMargin",   "paragraphIndent",
    "embeddedStyle", "hyphenationEnabled", "readerInkWeight", "textAntiAliasing"};
inline constexpr int TEXT_KEY_COUNT = static_cast<int>(sizeof(TEXT_KEYS) / sizeof(TEXT_KEYS[0]));
static_assert(TEXT_KEY_COUNT < PIN_TEXT, "a text pin fits under the flag");
// The pin of "text/<key>", or 0 when the key is no text setting.
inline uint8_t textPin(const char* storedKey) {
  if (!storedKey || std::strncmp(storedKey, "text/", 5) != 0) return 0;
  for (int i = 0; i < TEXT_KEY_COUNT; i++)
    if (std::strcmp(storedKey + 5, TEXT_KEYS[i]) == 0) return static_cast<uint8_t>(PIN_TEXT | i);
  return 0;
}

// Giu nut Len hoac Xuong trong tab Yeu thich la goi ham nay: day muc o `viTri` di mot
// bac theo `huong` (-1 len, 1 xuong). QUAY VONG, vi ca may quay vong, va vi nho vay dua
// mot muc tu day len dau chi ton mot nhip. Tra ve vi tri moi cua muc do.
int dayBac(std::vector<Action>& ghim, int viTri, int huong);

}  // namespace readermenu
