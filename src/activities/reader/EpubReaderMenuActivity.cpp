#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "activities/settings/SettingsTabs.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId TAB_NAMES[readermenu::TAB_COUNT] = {StrId::STR_READER_TAB_FAVORITES, StrId::STR_READER_TAB_POSITION,
                                                    StrId::STR_READER_TAB_READING, StrId::STR_READER_TAB_TOOLS};
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks)
    : UiTabListActivity("EpubReaderMenu", renderer, mappedInput),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  readermenu::buildItems(menuItems, hasFootnotes, hasBookmarks, Frontlight.present());
  napGhim();
  rebuildRows();
}

// Fills menuRowItems/rowToItem for the active tab. Structural: call it when the
// active tab changes, never from buildScreen(), which only refreshes the value
// text of rows that carry live state.
void EpubReaderMenuActivity::rebuildRows() {
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
    menuRowItems[r] = item;
  }
}

const char* EpubReaderMenuActivity::tabLabel(const int index) const { return I18N.get(TAB_NAMES[index]); }

freeink::ui::BitmapRef EpubReaderMenuActivity::tabIcon(const int index) const {
  if (SETTINGS.uiTheme != CrossPointSettings::TENOR_UI || index < 0 || index >= readermenu::TAB_COUNT) return {};
  static const freeink::Icon* const icons[] = {&icon_tenor_home_favorites_32, &icon_tenor_reader_position_32,
                                               &icon_tenor_reader_reading_32, &icon_tenor_reader_tools_32};
  freeink::ui::BitmapRef result;
  result.data = icons[index]->bits;
  result.width = icons[index]->w;
  result.height = icons[index]->h;
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
  activeTabId = tab;
  rebuildRows();
  // Pull the viewport to this tab's remembered row. UiTabListActivity owns the
  // remember/forget rule for every tab screen; see rowTab there.
  if (!mappedInput.hasTouch() && rowCount > 0 && activeNav().selected <= 0) activeNav().selected = 1;
  activeNav().followOnBuild = true;
}

void EpubReaderMenuActivity::stepTab(const int direction) {
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
    const auto action = static_cast<MenuAction>(SETTINGS.readerFavorites[i]);
    if (action != MenuAction::FONT_SIZE && action != MenuAction::FONT_FAMILY) favorites.push_back(action);
  }
  // Chua tung ghim gi thi lay ban mac dinh. Danh sach RONG la mot lua chon that: nguoi
  // doc go het moi muc ra, va luc do tab Yeu thich phai rong chu khong tu moc lai.
  if (SETTINGS.readerFavoriteCount == 0 && !SETTINGS.readerFavoritesDaDat) {
    favorites.assign(std::begin(readermenu::DEFAULT_FAVORITES), std::end(readermenu::DEFAULT_FAVORITES));
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

  const auto selectedAction = menuItems[rowToItem[index]].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
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
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
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
    chonCoChu();
    return;
  }
  if (selectedAction == MenuAction::FONT_FAMILY) {
    chonHoFont();
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

void EpubReaderMenuActivity::chonCoChu() {
  const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  const int n = static_cast<int>(sizes.size());
  if (n <= 0) {
    dongVoi(MenuAction::FONT_SIZE, 0, -1);
    return;
  }
  const int cur = fontdoc::coDangDung(sizes);
  if (!settingstabs::moTrinhChon(n)) {
    dongVoi(MenuAction::FONT_SIZE, sizes[(cur + 1) % n], -1);  // duoi 4: doi tai cho, quay vong
    return;
  }
  std::vector<std::string> nhan;
  nhan.reserve(n);
  for (const uint8_t pt : sizes) nhan.push_back(std::to_string(pt) + " pt");
  if (!optionPopup.vuaMan(renderer, nhan)) {
    dongVoi(MenuAction::FONT_SIZE, 0, -1);  // qua nhieu co cho mot popup: mo man day du
    return;
  }
  optionPopup.show(StrId::STR_FONT_SIZE, nhan, cur, [this, sizes, cur](const int idx) {
    if (idx < 0 || idx >= static_cast<int>(sizes.size()) || idx == cur) return;  // chon lai co cu: 0 dan lai
    dongVoi(MenuAction::FONT_SIZE, sizes[idx], -1);
  });
  requestUpdate();
}

void EpubReaderMenuActivity::chonHoFont() {
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
  if (!optionPopup.vuaMan(renderer, nhan)) {
    dongVoi(MenuAction::FONT_FAMILY, 0, -1);  // 130 ho khong vua mot popup: man Cai dat van ban co cuon
    return;
  }
  optionPopup.show(StrId::STR_FONT_FAMILY, nhan, cur, [this, n, cur](const int idx) {
    if (idx < 0 || idx >= n || idx == cur) return;
    dongVoi(MenuAction::FONT_FAMILY, 0, static_cast<int8_t>(idx));
  });
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
  // dang trong o man nay, va dieu mot cua tenor/cross la it nut nhat (CLAUDE.md).
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
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(tenorchrome::enabled() ? tenorchrome::TAB_TOP
                                                  : safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  buildTabBar(screen);

  // Progress summary under the tab band.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
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
    } else if (action == MenuAction::FRONTLIGHT) {
      menuRowItems[r].value = I18N.get(Frontlight.isOn() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    }
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
  if (activeTabId == MenuTab::FAVORITES)
    screen.takeBottom(static_cast<int16_t>(tenorchrome::tipHeight(renderer, tr(STR_FAVORITES_HINT), 4)));
  syncTabListViewport(screen, props);
  screen.list(props);

  if (activeTabId == MenuTab::FAVORITES) tenorchrome::drawTip(renderer, tr(STR_FAVORITES_HINT), 0, 4);
}

void EpubReaderMenuActivity::drawChrome() {
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, tabLabel(activeTab()), tr(STR_READER_MENU));
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}

bool EpubReaderMenuActivity::rowIsPinned(int row) const {
  return row >= 0 && row < rowCount &&
         std::find(favorites.begin(), favorites.end(), menuItems[rowToItem[row]].action) != favorites.end();
}
