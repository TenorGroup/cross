#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <I18n.h>

#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MenuCustomization.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "ReaderToolbarUi.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyChrome.h"
#include "shells/ugly/UglyInk.h"
#include "SdCardFontSystem.h"
#include "activities/settings/SettingsTabs.h"
#include "components/TenorMenuChrome.h"
#include "components/UIScale.h"
#include "components/SettledListRender.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "components/icons/X3ReaderTabIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId TAB_NAMES[readermenu::X3_TAB_COUNT] = {StrId::STR_READER_TAB_FAVORITES, StrId::STR_READER_TAB_POSITION,
                                                       StrId::STR_READER_TAB_READING, StrId::STR_READER_TAB_TOOLS,
                                                       StrId::STR_TOOL_TEXT};
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks, const bool textOnly)
    : UiTabListActivity("EpubReaderMenu", renderer, mappedInput),
      title(title),
      textOnly(textOnly),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  readermenu::buildItems(menuItems, hasFootnotes, hasBookmarks, Frontlight.present(), halTiltSensor.isAvailable());
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    readermenu::separateTextTab(menuItems);
  }
  napGhim();
  if (textOnly || favorites.empty()) activeTabId = MenuTab::READING;
  rebuildRows();
}

// Fills menuRowItems/rowToItem for the active tab. Structural: call it when the
// active tab changes, never from buildScreen(), which only refreshes the value
// text of rows that carry live state.
void EpubReaderMenuActivity::rebuildRows() {
  if (textOnly) {
    rowCount = 0;
    for (size_t i = 0; i < menuItems.size() && rowCount < 1; ++i) {
      if (menuItems[i].action != MenuAction::TEXT_SETTINGS) continue;
      rowToItem[rowCount] = static_cast<uint8_t>(i);
      menuRowItems[rowCount].label = I18N.get(menuItems[i].labelId);
      menuRowItems[rowCount].actionValue = static_cast<int16_t>(rowCount);
      ++rowCount;
    }
    menuRowItems[rowCount].label = I18N.get(StrId::STR_BACK);
    menuRowItems[rowCount].actionValue = static_cast<int16_t>(rowCount);
    ++rowCount;
    return;
  }
  // ReaderMenuLayout owns which rows a tab shows and in what order; this only
  // turns the indexes it hands back into list rows.
  rowCount = activeTabId == MenuTab::FAVORITES
                 ? readermenu::rowsOfFavorites(menuItems, favorites.data(), static_cast<int>(favorites.size()),
                                               rowToItem, static_cast<int>(MAX_MENU_ITEMS))
                 : readermenu::rowsOfTab(menuItems, activeTabId, rowToItem, static_cast<int>(MAX_MENU_ITEMS));
  for (int r = 0; r < rowCount; r++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[rowToItem[r]].labelId);
    item.actionValue = static_cast<int16_t>(r);
    const auto style = rowChoiceStyle(r);
    item.opensNext = style != SettingsChoiceStyle::None
                        ? style == SettingsChoiceStyle::Page : readermenu::rowOpens(menuItems[rowToItem[r]].action);
    menuRowItems[r] = item;
  }
}

int EpubReaderMenuActivity::settingsChoiceCount(const int row) const {
  if (row < 0 || row >= rowCount || textOnly) return 0;
  switch (menuItems[rowToItem[row]].action) {
    case MenuAction::ROTATE_SCREEN: return orientationLabels.size();
    case MenuAction::AUTO_PAGE_TURN: return pageTurnLabels.size();
    case MenuAction::STATUS_BAR: return CrossPointSettings::READER_STATUS_BAR_MODE_COUNT;
    case MenuAction::FONT_SIZE: return readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName).size();
    case MenuAction::FONT_FAMILY: return fontdoc::danhSachHo(&sdFontSystem.registry()).size();
    case MenuAction::NIGHT_MODE:
    case MenuAction::TILT_PAGE_TURN:
    case MenuAction::FRONTLIGHT: return 2;
    default: return 0;
  }
}

SettingsChoiceStyle EpubReaderMenuActivity::rowChoiceStyle(const int row) const {
  if (row < 0 || row >= rowCount) return SettingsChoiceStyle::None;
  const auto action = menuItems[rowToItem[row]].action;
  const bool onOff = action == MenuAction::NIGHT_MODE || action == MenuAction::TILT_PAGE_TURN || action == MenuAction::FRONTLIGHT;
  return settingsChoiceStyle(settingsChoiceCount(row), tenorchrome::kTouchShell, onOff);
}

const char* EpubReaderMenuActivity::tabLabel(const int index) const {
  return I18N.get(textOnly ? StrId::STR_TOOL_TEXT : TAB_NAMES[index]);
}

freeink::ui::BitmapRef EpubReaderMenuActivity::tabIcon(const int index, const bool bold) const {
  if (index < 0 || index >= tabCount()) return {};
  static const freeink::Icon* const icons[] = {&icon_tenor_home_favorites_40, &icon_tenor_reader_position_40,
                                               &icon_tenor_reader_reading_40, &icon_tenor_reader_tools_40,
                                               &icon_tenor_reader_reading_40};
  static const freeink::Icon* const bolds[] = {&icon_tenor_home_favorites_bold_40, &icon_tenor_reader_position_bold_40,
                                               &icon_tenor_reader_reading_bold_40, &icon_tenor_reader_tools_bold_40,
                                               &icon_tenor_reader_reading_bold_40};
  const freeink::Icon* const* set = bold ? bolds : icons;
  freeink::ui::BitmapRef result;
  const int slot = textOnly && tenorchrome::x3ScreenLayoutEnabled() ? static_cast<int>(MenuTab::TEXT) : index;
  const freeink::Icon* icon = set[slot];
  if (tenorchrome::x3ScreenLayoutEnabled() && !shell::uglyParts())
    icon = &readermenu::x3TabIcon(static_cast<MenuTab>(slot), bold);
  else if (tenorchrome::x3ScreenLayoutEnabled() && !textOnly && slot == static_cast<int>(MenuTab::READING))
    icon = bold ? &icon_tenor_home_recent_bold_40 : &icon_tenor_home_recent_40;
  result.data = icon->bits;
  result.width = icon->w;
  result.height = icon->h;
  result.format = freeink::ui::BitmapFormat::Mask1;
  result.progmem = false;
  return result;
}

void EpubReaderMenuActivity::selectTab(const MenuTab tab) {
  // The render task reads activeTabId, rowCount, menuRowItems and rowToItem
  // while it builds the screen, and a tab step arrives from the button path
  // with no lock held. Swapping the row set mid-build would draw one tab's
  // labels against another's values, and Confirm would look the activated row
  // up in whichever mapping won. Same race moveSelectionTo() guards for nav.
  RenderLock lock(*this);
  activeTabId = tab == MenuTab::FAVORITES && favorites.empty() ? MenuTab::READING : tab;
  rebuildRows();
  // Pull the viewport to this tab's remembered row. UiTabListActivity owns the
  // remember/forget rule for every tab screen; see rowTab there.
  if (!mappedInput.hasTouch() && rowCount > 0 && activeNav().selected <= 0) activeNav().selected = 1;
  activeNav().followOnBuild = true;
}

void EpubReaderMenuActivity::stepTab(const int direction) {
  if (textOnly) return;
  const int next = adjacentTab(direction);
  selectTab(static_cast<MenuTab>(next));
  requestUpdate();
}

void EpubReaderMenuActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectTab(static_cast<MenuTab>(index));
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void EpubReaderMenuActivity::napGhim() {
  favorites.clear();
  for (uint8_t i = 0; i < SETTINGS.readerFavoriteCount && i < CrossPointSettings::READER_FAVORITE_MAX; i++) {
    if (SETTINGS.readerFavorites[i] & readermenu::PIN_TEXT) continue;  // an X4 Pro text pin
    const auto action = static_cast<MenuAction>(SETTINGS.readerFavorites[i]);
    // Touch: the font size row opens its popup in the menu, so it may be pinned.
    if ((action != MenuAction::FONT_SIZE || tenorchrome::kTouchShell) && action != MenuAction::FONT_FAMILY)
      favorites.push_back(action);
  }
  // Chua tung ghim gi thi lay ban mac dinh. Danh sach RONG la mot lua chon that: nguoi
  // doc go het moi muc ra, va luc do tab Yeu thich phai rong chu khong tu moc lai.
  if (SETTINGS.readerFavoriteCount == 0 && !SETTINGS.readerFavoritesDaDat) {
    favorites.assign(std::begin(readermenu::DEFAULT_FAVORITES), std::end(readermenu::DEFAULT_FAVORITES));
    if (tenorchrome::kTouchShell) favorites.insert(favorites.begin(), MenuAction::FONT_SIZE);
  }
}

void EpubReaderMenuActivity::luuGhim() {
  const size_t n = favorites.size() < CrossPointSettings::READER_FAVORITE_MAX ? favorites.size()
                                                                              : CrossPointSettings::READER_FAVORITE_MAX;
  for (size_t i = 0; i < n; i++) SETTINGS.readerFavorites[i] = static_cast<uint8_t>(favorites[i]);
  SETTINGS.readerFavoriteCount = static_cast<uint8_t>(n);
  SETTINGS.readerFavoritesDaDat = 1;
  SETTINGS.saveToFile();
}

bool EpubReaderMenuActivity::doiGhimDongDangChon() {
  const int dong = ringPos() - 1;
  if (dong < 0 || dong >= rowCount) return false;
  const MenuAction viec = menuItems[rowToItem[dong]].action;

  {
    // Tac ve doc favorites, rowToItem va menuRowItems trong luc dung man, con nhip nut
    // nay toi tu duong khong giu khoa. Cung cuoc dua selectTab() da chan.
    RenderLock lock(*this);
    if (!readermenu::doiGhim(favorites, viec)) return true;  // day roi: nuot nhip, dung im
    rebuildRows();
    // Go mot muc trong chinh tab Yeu thich lam danh sach ngan lai, nen con tro co the
    // tro ra ngoai. Keo no ve dong cuoi con lai, hoac ve thanh the neu het sach.
    if (activeTabId == MenuTab::FAVORITES && ringPos() > rowCount) activeNav().selected = rowCount;
  }
  luuGhim();
  requestUpdate();
  return true;
}

bool EpubReaderMenuActivity::xepLaiDongDangChon(const int huong) {
  const int dong = ringPos() - 1;
  if (dong < 0 || dong >= rowCount) return false;

  {
    RenderLock lock(*this);
    // Dong thu `dong` cua tab Yeu thich la muc thu `dong` cua danh sach ghim, tru khi
    // co muc ghim khong ton tai o cuon sach nay (vi du Den nen tren X3) va bi bo qua.
    // Nen tim theo VIEC chu khong theo so thu tu.
    const MenuAction viec = menuItems[rowToItem[dong]].action;
    int viTri = -1;
    for (size_t i = 0; i < favorites.size(); i++) {
      if (favorites[i] == viec) viTri = static_cast<int>(i);
    }
    if (viTri < 0) return false;

    readermenu::dayBac(favorites, viTri, huong);
    rebuildRows();
    // Con tro bam theo muc vua day, chu khong dung yen tai cho cu.
    for (int r = 0; r < rowCount; r++) {
      if (menuItems[rowToItem[r]].action == viec) {
        activeNav().selected = r + 1;
        break;
      }
    }
  }
  luuGhim();
  requestUpdate();
  return true;
}

bool EpubReaderMenuActivity::giuNutDiDong(const int huong) {
  if (activeTabId != MenuTab::FAVORITES) return false;

  // Nuot moi nhip lap cua cap nut di-dong trong tab nay, ke ca nhip chua toi nguong giu.
  // Neu de nhip lap chay tiep thi con tro vua di vua keo muc, khong ai theo kip.
  const auto nut = huong > 0 ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  // wasLongPressed ban DUNG MOT LAN moi lan bam, va nuot luot nha ke tiep. Do 14/09:
  // moc vao nhip lap thi mot lan giu 1500ms day muc di HAI bac, va bac thu hai con quay
  // vong tu dau danh sach xuong cuoi.
  if (!mappedInput.wasLongPressed(nut, GIU_MS)) return true;
  return xepLaiDongDangChon(huong);
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  // Touch: the Home key leaves the book (Home), "<" in the bar closes the menu.
  if (tenorchrome::kTouchShell) return false;
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  commitTabNavigation();
  if (optionPopup.isActive()) return;
  if (index < 0 || index >= rowCount) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();

  if (textOnly) {
    if (index == rowCount - 1) {
      closeCancelled();
    } else {
      setResult(MenuResult{static_cast<int>(MenuAction::TEXT_SETTINGS), pendingOrientation, selectedPageTurnOption});
      finish();
    }
    return;
  }

  const auto selectedAction = menuItems[rowToItem[index]].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    showSettingsChoices(optionPopup, StrId::STR_ORIENTATION, orientationLabels.data(), orientationLabels.size(),
                        pendingOrientation, index, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    const auto callback = [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     };
    std::vector<std::string> labels;
    for (const auto* label : pageTurnLabels) labels.emplace_back(label);
    showSettingsChoices(optionPopup, StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels,
                        selectedPageTurnOption, index, callback);
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::TILT_PAGE_TURN) {
    SETTINGS.toggleTiltPageTurn();
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::STATUS_BAR) {
    // Sau muc thanh trang thai cua trinh doc: popup de doc ten tung muc, chon xong
    // ghi lai. Doi giua nam muc cung chieu cao thi chi ve lai thanh; doi sang/tu
    // muc Tat thi chieu cao doi, trinh doc se dan lai trang va giu anchor.
    std::vector<std::string> chu;
    chu.reserve(CrossPointSettings::READER_STATUS_BAR_MODE_COUNT);
    for (const auto id : readermenu::STATUS_BAR_MODE_LABELS) chu.push_back(I18N.get(id));
    const int dangDung = SETTINGS.readerStatusBarMode;
    const auto callback = [this](const int idx) {
      if (idx < 0 || idx >= CrossPointSettings::READER_STATUS_BAR_MODE_COUNT) return;
      SETTINGS.readerStatusBarMode = static_cast<uint8_t>(idx);
      SETTINGS.saveToFile();
    };
    showSettingsChoices(optionPopup, StrId::STR_HIDE_READER_STATUS_BAR, chu, dangDung, index, callback);
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FONT_SIZE) {
    chonCoChu(index);
    return;
  }
  if (selectedAction == MenuAction::FONT_FAMILY) {
    chonHoFont(index);
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

void EpubReaderMenuActivity::dongVoi(const MenuAction viec, const uint8_t coChu, const int8_t hoFont) {
  MenuResult r;
  r.action = static_cast<int>(viec);
  r.orientation = pendingOrientation;
  r.pageTurnOption = selectedPageTurnOption;
  r.coChu = coChu;
  r.hoFont = hoFont;
  setResult(std::move(r));
  finish();
}

void EpubReaderMenuActivity::chonCoChu(const int row) {
  const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  const int n = static_cast<int>(sizes.size());
  if (n <= 0) {
    dongVoi(MenuAction::FONT_SIZE, 0, -1);
    return;
  }
  const int cur = fontdoc::coDangDung(sizes);
  if (!settingstabs::moTrinhChon(n)) {
    dongVoi(MenuAction::FONT_SIZE, sizes[(cur + 1) % n], -1);  // 1 lua chon: giu gia tri hien tai
    return;
  }
  std::vector<std::string> nhan;
  nhan.reserve(n);
  for (const uint8_t pt : sizes) nhan.push_back(std::to_string(pt) + " pt");
  const auto parentFrame = app.publishedRect(ACTION_ROW, row);
  const bool inFrame = tenorchrome::kTouchShell && parentFrame.height > 0;
  if (settingsChoiceStyle(n, tenorchrome::kTouchShell) == SettingsChoiceStyle::Page ||
      (!inFrame && !optionPopup.vuaMan(renderer, nhan))) {
    dongVoi(MenuAction::FONT_SIZE, 0, -1);  // qua nhieu co cho mot popup: mo man day du
    return;
  }
  const auto onSelect = [this, sizes, cur](const int idx) {
    if (idx < 0 || idx >= static_cast<int>(sizes.size()) || idx == cur) return;  // chon lai co cu: 0 dan lai
    dongVoi(MenuAction::FONT_SIZE, sizes[idx], -1);
  };
  if (inFrame)
    optionPopup.showInFrame(parentFrame, StrId::STR_FONT_SIZE, nhan, cur, onSelect);
  else
    optionPopup.show(StrId::STR_FONT_SIZE, nhan, cur, onSelect);
  requestUpdate();
}

void EpubReaderMenuActivity::chonHoFont(const int row) {
  const std::vector<fontdoc::Ho> ho = fontdoc::danhSachHo(&sdFontSystem.registry());
  const int n = static_cast<int>(ho.size());
  if (n <= 0) {
    dongVoi(MenuAction::FONT_FAMILY, 0, -1);
    return;
  }
  const int cur = fontdoc::hoDangDung(&sdFontSystem.registry());
  if (!settingstabs::moTrinhChon(n)) {
    dongVoi(MenuAction::FONT_FAMILY, 0, static_cast<int8_t>((cur + 1) % n));
    return;
  }
  std::vector<std::string> nhan;
  nhan.reserve(n);
  for (const auto& h : ho) nhan.push_back(h.ten);
  const auto parentFrame = app.publishedRect(ACTION_ROW, row);
  const bool inFrame = tenorchrome::kTouchShell && parentFrame.height > 0;
  if (settingsChoiceStyle(n, tenorchrome::kTouchShell) == SettingsChoiceStyle::Page ||
      (!inFrame && !optionPopup.vuaMan(renderer, nhan))) {
    dongVoi(MenuAction::FONT_FAMILY, 0, -1);  // 130 ho khong vua mot popup: man Cai dat van ban co cuon
    return;
  }
  const auto onSelect = [this, n, cur](const int idx) {
    if (idx < 0 || idx >= n || idx == cur) return;
    dongVoi(MenuAction::FONT_FAMILY, 0, static_cast<int8_t>(idx));
  };
  if (inFrame)
    optionPopup.showInFrame(parentFrame, StrId::STR_FONT_FAMILY, nhan, cur, onSelect);
  else
    optionPopup.show(StrId::STR_FONT_FAMILY, nhan, cur, onSelect);
  requestUpdate();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Quay lai la ROI MAN, mot nhip, o moi vi tri. Truoc 14/09/2026 dem nhip dau chi dua con
    // tro ve thanh the: nhip do khong mua duoc gi, vi hai nut canh da nhay the tu moi dong.
    closeCancelled();
    return true;
  }

  // Giu Chon tren mot dong la GHIM dong do vao Yeu thich, giu lan nua la go ra. Nhip giu
  // dang trong o man nay, va dieu mot cua tenor/cross la it nut nhat.
  //
  // Dung wasLongPressed chu khong tu do getHeldTime luc nha: ham nay ban ngay khi qua
  // nguong, va no NUOT luot nha ke tiep, nen giu khong bi tinh thanh mot lan bam nhanh
  // roi chay lenh cua dong do. Do 14/09: tu do lay gio luc nha thi giu 1500ms van mo
  // man Chon chuong.
  if (ringPos() > 0 && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GIU_MS)) {
    doiGhimDongDangChon();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      moveRingTo(1);  // step into the tab's first row; the edge buttons change tab
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false, UITheme::StatusBarScope::Reader);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(tenorchrome::enabled() ? tenorchrome::tabTop()
                                                  : safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  skinTab_ = screen.body();
  skinTab_.height = static_cast<int16_t>(preferredTabBarHeight());
  buildTabBar(screen);
  const bool x3 = tenorchrome::x3ScreenLayoutEnabled();
  if (x3) {
    const auto layout = screenLayout();
    const auto body = layout.bodyRect;
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(body.y), 0,
                                                  static_cast<int16_t>(renderer.getScreenHeight() - body.bottom()), 0});
    skinTab_ = {static_cast<int16_t>(layout.barRect.x), static_cast<int16_t>(layout.barRect.y),
                static_cast<int16_t>(layout.barRect.width), static_cast<int16_t>(layout.barRect.height)};
  }
  if (!tenorchrome::kTouchShell) {
    // buildTabBar uses the full logical screen band, with its measured height.
    skinTab_.x = screen.frame().screen().x;
    skinTab_.width = screen.frame().screen().width;
    if (!x3) skinTab_.height = static_cast<int16_t>(screen.body().y - skinTab_.y - metrics.verticalSpacing);
    if (tabWindowCount() < tabCount()) {
      skinTab_.x = static_cast<int16_t>(skinTab_.x + MUI_TEN_LE);
      skinTab_.width = static_cast<int16_t>(skinTab_.width - 2 * MUI_TEN_LE);
    }
  }

  // Progress summary under the tab band.
  const auto progressLine = progressText();
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(
      x3 ? (infoOverBar() ? 0 : tenorchrome::tipHeight(renderer, progressLine.c_str())) : metrics.tabBarHeight));
  skinProgress_ = band;
  const int16_t pad = screen.theme().headerSidePadding;
  if (x3)
    drawX3Content();
  else
    screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rebuildRows() set each row's label; only rows with live values change here.
  for (int r = 0; r < rowCount; r++) {
    const auto action = menuItems[rowToItem[r]].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[r].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[r].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::NIGHT_MODE) {
      menuRowItems[r].value = I18N.get(SETTINGS.screenInverted ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::TILT_PAGE_TURN) {
      menuRowItems[r].value = I18N.get(SETTINGS.tiltPageTurn ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::FRONTLIGHT) {
      menuRowItems[r].value = I18N.get(Frontlight.isOn() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    }
  }

  for (int row = 0; row < rowCount; ++row) {
    const auto action = menuItems[rowToItem[row]].action;
    menuRowItems[row].toggle = rowChoiceStyle(row) == SettingsChoiceStyle::Inline;
    menuRowItems[row].toggleChecked = action == MenuAction::NIGHT_MODE ? SETTINGS.screenInverted
        : action == MenuAction::TILT_PAGE_TURN ? SETTINGS.tiltPageTurn : Frontlight.isOn();
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  skinHint_ = {};
  if (!x3 && activeTabId == MenuTab::FAVORITES)
    skinHint_ = screen.takeBottom(static_cast<int16_t>(tenorchrome::tipHeight(renderer, tr(STR_FAVORITES_HINT), 4)));
  syncTabListViewport(screen, props);
  pinLabelStyle_ = props.labelText;
  pinTextPad_ = screen.resolveListProps(props).sidePadding;
  screen.list(props);

  if (!x3 && activeTabId == MenuTab::FAVORITES && !shell::isUgly())
    tenorchrome::drawTip(renderer, tr(STR_FAVORITES_HINT), 0, 4);
}

// The progress line stands over the bar, where the tips stand (the X3's bottom layout); tenor/ugly keeps it at the top.
bool EpubReaderMenuActivity::infoOverBar() const { return tenorchrome::x3ScreenLayoutEnabled() && !shell::uglyParts(); }

tenorchrome::X3ScreenLayout EpubReaderMenuActivity::screenLayout() const {
  return tenorchrome::x3ScreenLayout(
      renderer, activeTabId == MenuTab::FAVORITES ? tenorchrome::tipHeight(renderer, tr(STR_FAVORITES_HINT), 4) : 0,
      infoOverBar() ? tenorchrome::tipHeight(renderer, progressText().c_str(), 2) : 0);
}

std::string EpubReaderMenuActivity::progressText() const {
  std::string text;
  if (totalPages > 0)
    text = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" + std::to_string(totalPages) +
           tr(STR_PAGES_SEPARATOR);
  text += bookProgressPercent >= 0 ? std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%"
                                   : std::string(tr(STR_INDEXING));
  return text;
}

void EpubReaderMenuActivity::drawX3Content() const {
  const auto layout = screenLayout();
  const auto text = progressText();
  if (infoOverBar()) {
    const int infoHeight = tenorchrome::tipHeight(renderer, text.c_str(), 2);
    auto tips = layout.tipRect;
    tenorchrome::drawInfoInRect(renderer, text.c_str(), {tips.x, tips.y, tips.width, static_cast<int16_t>(infoHeight)}, 2);
    tips.y = static_cast<int16_t>(tips.y + infoHeight);
    tips.height = static_cast<int16_t>(tips.height - infoHeight);
    if (activeTabId == MenuTab::FAVORITES) tenorchrome::drawTipInRect(renderer, tr(STR_FAVORITES_HINT), tips, 4);
    return;
  }
  tenorchrome::drawInfoInRect(renderer, text.c_str(), layout.bodyRect);
  if (activeTabId == MenuTab::FAVORITES)
    tenorchrome::drawTipInRect(renderer, tr(STR_FAVORITES_HINT), layout.tipRect, 4);
}

void EpubReaderMenuActivity::drawChrome() {
  if (tenorchrome::x3ScreenLayoutEnabled()) return;
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, tabLabel(activeTab()), tr(STR_READER_MENU));
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false, UITheme::StatusBarScope::Reader);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  const bool handwritten = shell::isUgly();
  uiTarget.setPaintingEnabled(!handwritten);
  renderSettledList(activeNav(), [&] {
    if (!handwritten) {
      renderer.clearScreen();
      drawChrome();
    }
    renderUi();
  });
  uiTarget.setPaintingEnabled(true);
  if (handwritten) paintUglyMenu();
  drawFooter();
  renderer.displayBuffer();
}

// The pinned rows' heart stands right after the row's name, as in the toolbar panels (one mark, one place).
void EpubReaderMenuActivity::drawPinMarks() {
  if (!uiTarget.paintingEnabled()) return;
  const int lineHeight = uiTarget.lineHeight(pinLabelStyle_.font);
  for (int row = activeNav().top; row < rowCount; ++row) {
    const auto box = app.publishedRect(ACTION_ROW, row);
    if (box.empty() || !rowIsPinned(row)) continue;
    const int width = uiTarget.measureText(pinLabelStyle_.font, menuRowItems[row].label, pinLabelStyle_).width;
    tenorchrome::drawFavoriteMark(renderer, box.x + pinTextPad_ + width + 4, uiScaleSpec().bodyFontId,
                                  box.y + (box.height - lineHeight) / 2);
  }
}

bool EpubReaderMenuActivity::rowIsPinned(int row) const {
  if (textOnly) return false;
  return row >= 0 && row < rowCount &&
         std::find(favorites.begin(), favorites.end(), menuItems[rowToItem[row]].action) != favorites.end();
}

void EpubReaderMenuActivity::paintUglyMenu() {
  const int width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool inBar = tenorchrome::x3ScreenLayoutEnabled();  // button boards: the names stand in the bar under the paper
  const int bottom = inBar ? screenLayout().tipRect.y - 4  // the tip, when the tab has one, stands between the paper and the bar
      : height - (tenorchrome::kTouchShell ? tenorchrome::footBackReserve() : metrics.buttonHintsHeight);
  const fui::Rect frame{16, static_cast<int16_t>(tenorchrome::kTouchShell ? tenorchrome::contentTop() : 8),
                        static_cast<int16_t>(width - 32), static_cast<int16_t>(bottom - (tenorchrome::kTouchShell ? tenorchrome::contentTop() : 8))};
  readerugly::paper(renderer, frame);
  if (!tenorchrome::kTouchShell && !inBar) tenorchrome::drawHeader(renderer, tr(STR_READER_MENU));
  const int shownTabs = tabWindowCount();
  const auto nameWidth = [&](const int slot) {
    return ugly::width(renderer, ugly::Size::S22,
                       tabLabel(menucustom::idAt(menucustom::groupFor(name.c_str()), tabWindowStart() + slot, tabCount())));
  };
  // Each name its own width and the gaps equal, 32 px in from the bar's ends.
  int sum = 0;
  for (int slot = 0; slot < shownTabs; ++slot) sum += nameWidth(slot);
  const int span = skinTab_.width - 64, fit = std::max(sum, span);  // names that do not fit shrink together
  const int gap = shownTabs > 1 ? (span - sum * span / fit) / (shownTabs - 1) : 0;
  int pen = skinTab_.x + 32;
  for (int slot = 0; slot < shownTabs; ++slot) {
    const int tab = menucustom::idAt(menucustom::groupFor(name.c_str()), tabWindowStart() + slot, tabCount());
    auto box = app.publishedRect(ACTION_TAB, tab);
    const bool hit = !box.empty();
    if (!hit) {
      const int wide = nameWidth(slot) * span / fit;
      // The words ride 8 px over the bar's middle: the circle (20 px each way) clears the foot below.
      box = {static_cast<int16_t>(shownTabs > 1 ? pen - 8 : skinTab_.x + (skinTab_.width - wide) / 2 - 8), skinTab_.y,
             static_cast<int16_t>(wide + 16), static_cast<int16_t>(skinTab_.height - 16)};
      pen += wide + gap;
    }
    const auto ink = readerugly::text(renderer, box.inset(fui::Insets{0, 8, 0, 8}), tabLabel(tab), fui::TextAlign::Center);
    // The circle grows 28 px each way of the box it goes round: round the middle of the words (16 px in from each
    // end, 8 px high) it keeps 12 px clear of the name, between its neighbours and inside the bar.
    if (tab == activeTab()) uglychrome::ring(renderer, hit ? ink : fui::Rect{static_cast<int16_t>(ink.x + 16),
        static_cast<int16_t>(ink.y + 9), static_cast<int16_t>(std::max(4, ink.width - 32)), 8}, tabMark_.get(activeTab()));
  }
  std::string summary;
  if (totalPages > 0) summary = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                               std::to_string(totalPages) + tr(STR_PAGES_SEPARATOR);
  summary += bookProgressPercent >= 0 ? std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%"
                                     : std::string(tr(STR_INDEXING));
  readerugly::text(renderer, skinProgress_.inset(fui::Insets{0, 16, 0, 16}), summary.c_str());
  for (int row = activeNav().top; row < rowCount; ++row) {
    const auto box = app.publishedRect(ACTION_ROW, row);
    if (box.empty()) continue;
    const auto& item = menuRowItems[row];
    const bool pinned = rowIsPinned(row);
    auto label = box.inset(fui::Insets{0, 16, 0, 16});
    label.width = static_cast<int16_t>(std::max(0, label.width - (pinned ? 40 : 0) - (item.opensNext ? 28 : 0)));
    if (item.value && *item.value) {
      const int valueWidth = std::min<int>(label.width / 2, ugly::width(renderer, ugly::Size::S22, item.value));
      readerugly::text(renderer, {static_cast<int16_t>(label.right() - valueWidth), label.y,
                                 static_cast<int16_t>(valueWidth), label.height}, item.value, fui::TextAlign::Right);
      label.width = static_cast<int16_t>(std::max(0, label.width - valueWidth - 12));
    }
    const auto ink = readerugly::text(renderer, label, item.label);
    if (row == ringPos() - 1) uglychrome::ring(renderer, ink,
        mark_.get(selectionMarkKey()));
    if (pinned) ugly::tick(renderer, box.right() - 26 - (item.opensNext ? 28 : 0), box.y + box.height / 2);
    if (item.opensNext) ugly::mark(renderer, ugly::Mark::Right, box.right() - 14, box.y + box.height / 2);
  }
  if (!skinHint_.empty() || (inBar && activeTabId == MenuTab::FAVORITES))
    tenorchrome::drawTip(renderer, tr(STR_FAVORITES_HINT), 0, 4);
}
