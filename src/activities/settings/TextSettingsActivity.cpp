#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Tab labels for Font | Size | Layout | Style.
constexpr StrId TAB_NAME_IDS[] = {StrId::STR_FONT, StrId::STR_SIZE, StrId::STR_LAYOUT, StrId::STR_STYLE};

constexpr StrId LAYOUT_ROW_NAME_IDS[] = {StrId::STR_LINE_SPACING, StrId::STR_EXTRA_SPACING, StrId::STR_ALIGNMENT,
                                         StrId::STR_SCREEN_MARGIN, StrId::STR_PARAGRAPH_INDENT};
constexpr StrId STYLE_ROW_NAME_IDS[] = {StrId::STR_FOCUS_READING, StrId::STR_HYPHENATION, StrId::STR_EMBEDDED_STYLE,
                                        StrId::STR_TEXT_AA, StrId::STR_READER_INK_WEIGHT};

constexpr StrId INK_WEIGHT_IDS[] = {StrId::STR_INK_DEFAULT, StrId::STR_INK_LIGHT, StrId::STR_INK_STRONG};
constexpr StrId LINE_SPACING_IDS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr StrId INDENT_IDS[] = {StrId::STR_INDENT_AUTO, StrId::STR_STATE_ON, StrId::STR_STATE_OFF};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
constexpr int MARGIN_STEP = CrossPointSettings::SCREEN_MARGIN_STEP;
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab)
    : UiTabListActivity("TextSettings", renderer, mappedInput), registry_(registry), tab_(initialTab) {}

const char* TextSettingsActivity::tabLabel(const int index) const { return I18N.get(TAB_NAME_IDS[index]); }

void TextSettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = tenorchrome::enabled() ? tenorchrome::CONTENT_TOP
                                       : metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  // Danh sach ho va ho dang dung lay tu fontdoc, cung mot cho voi menu doc va giu nut.
  fonts_.clear();
  for (const auto& h : fontdoc::danhSachHo(registry_)) fonts_.push_back({h.ten, h.builtin, h.chiSo});

  rebuildSizeList();

  currentFamilyIndex_ = fontdoc::hoDangDung(registry_);
  for (auto& n : tabNavs) n.selected = 1;  // default to the first list row
  tabNavs[static_cast<int>(Tab::Family)].selected = currentFamilyIndex_ + 1;
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;

  rebuildRowItems();
}

// Rebuilds rowItems_ (label + actionValue) for the active tab. Structural -
// call only when tab_ or its backing data (fonts_/sizes_) changes, never from
// buildScreen(), which just refreshes rowValues_/rowItems_[].value in place.
void TextSettingsActivity::rebuildRowItems() {
  const int count = listCount();
  rowValues_.assign(count, std::string());
  rowItems_.clear();
  rowItems_.reserve(count);
  for (int i = 0; i < count; i++) {
    fui::ListItem item;
    switch (tab_) {
      case Tab::Family:
        item.label = fonts_[i].name.c_str();
        break;
      case Tab::Size:
        item.label = sizes_[i].name.c_str();
        break;
      case Tab::Layout:
        item.label = I18N.get(LAYOUT_ROW_NAME_IDS[i]);
        break;
      case Tab::Style:
        item.label = I18N.get(STYLE_ROW_NAME_IDS[i]);
        break;
      default:
        break;
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

// The selectable sizes belong to the active family, so this runs on entry and
// again after every family change. A family change goes through ensureLoaded(),
// which snaps SETTINGS.fontPointSize into the new family's set - but entry does
// not, so the highlight is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);

  // The stored size can still sit outside this family's set - e.g. the family
  // was deleted while selected, or the card was swapped. Highlight the size the
  // reader actually renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    // "pt" is deliberately not translated: it is the typographic unit symbol,
    // written the same way in every language CrossPoint ships.
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

void TextSettingsActivity::onTabAction(const int index) {
  if (optionPopup_.isActive()) return;
  if (tab_ != static_cast<Tab>(index)) {
    RenderLock lock(*this);
    tab_ = static_cast<Tab>(index);
    rebuildRowItems();
    auto& n = activeNav();
    if (!mappedInput.hasTouch() && listCount() > 0 && n.selected <= 0) n.selected = 1;
    n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
    requestUpdate();
  }
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void TextSettingsActivity::activateIndex(const int index) {
  if (optionPopup_.isActive()) return;
  // Most rows repaint a different surface (popup, preview, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  activateRow(index);
}

bool TextSettingsActivity::handleCustomInput() {
  return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool TextSettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Roi man mot nhip (chot 14/09/2026 dem), nhu menu doc va man Cai dat.
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      moveRingTo(1);  // cung luat voi ba man the kia: Chon o thanh the buoc xuong dong dau
    } else {
      activateRow(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void TextSettingsActivity::buildScreen(UiScreen& screen) {
  // Content sits below the preview pane (render() draws header + preview
  // directly) and above the caption band + button hints.
  const int tabTop = tenorchrome::enabled() ? tenorchrome::TAB_TOP : afterHeader + previewHeight;
  const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(tabTop), 0, static_cast<int16_t>(bottomReserved + captionHeight), 0});

  buildTabBar(screen);
  if (tenorchrome::enabled()) screen.takeTop(static_cast<int16_t>(previewHeight));

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the tab
  // was last switched; only the live value text needs refreshing here, by
  // assigning into the existing rowValues_ strings (no vector growth) rather
  // than building a new items/values vector on every render.
  const int count = listCount();
  for (int i = 0; i < count; i++) {
    switch (tab_) {
      case Tab::Family:
        rowValues_[i] = (i == currentFamilyIndex_) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Size:
        rowValues_[i] = (i == currentSizeIndex_) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Layout:
        rowValues_[i] = layoutValueText(i);
        break;
      case Tab::Style:
        rowValues_[i] = styleValueText(i);
        break;
      default:
        break;
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (see SettingsActivity).
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

const char* TextSettingsActivity::confirmLabelText() const {
  if (ringPos() == 0) return tr(STR_SELECT);
  switch (tab_) {
    case Tab::Layout:
      // Extra Paragraph Spacing toggles; the rest open a picker
      return ringPos() - 1 == static_cast<int>(LayoutRow::ParaSpacing) ||
                     ringPos() - 1 == static_cast<int>(LayoutRow::ParaIndent)
                 ? tr(STR_TOGGLE)
                 : tr(STR_SELECT);
    case Tab::Style:
      return ringPos() - 1 == static_cast<int>(StyleRow::InkWeight) ? tr(STR_SELECT) : tr(STR_TOGGLE);
    default:
      return tr(STR_SELECT);
  }
}

void TextSettingsActivity::render(RenderLock&&) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;  // picker draws over everything

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  drawNavigationHeader(tr(STR_TEXT_SETTINGS));

  const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                               ? fonts_[currentFamilyIndex_].name.c_str()
                               : "";
  const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                             ? sizes_[currentSizeIndex_].name.c_str()
                             : "";
  textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing, afterHeader,
                              previewHeight, familyName, sizeName);

  // Tab bar + active tab's list draw inside the screen builder.
  renderUi();

  if (focusedRowHasNoPreview()) {
    const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
    const int capY = afterHeader + usableHeight - captionHeight + metrics_.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics_.previewPadding, capY, tr(STR_NOT_IN_PREVIEW));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabelText(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the
// next one, and the render task walks that same object inside the preview's
// prewarmCache() - so without this lock a font switch can free the mini glyph
// arrays out from under prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
void TextSettingsActivity::applyFamily(int listIndex) {
  RenderLock lock;
  if (!fontdoc::apHo(renderer, registry_, listIndex)) return;  // switch failed, keep the old size list
  currentFamilyIndex_ = listIndex;

  // The new family ships its own set of point sizes, and ensureLoaded() may have
  // snapped the selection into it, so the Size tab's list and its nav position
  // both have to be rebuilt.
  rebuildSizeList();
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;
}

void TextSettingsActivity::activateRow(int row) {
  commitTabNavigation();
  switch (tab_) {
    case Tab::Family:
      if (row != currentFamilyIndex_) {
        applyFamily(row);
        // Persist immediately (like SettingsActivity's per-change saves): the
        // parent's result callback only runs on a normal finish(), so relying
        // on it loses the change when this screen is left via the home
        // gesture/key or a sleep. Saved here, not inside applyFamily, so the
        // SD write happens outside its RenderLock.
        if (currentFamilyIndex_ == row) {
          SETTINGS.saveToFile();
        }
        requestUpdate();
      }
      break;
    case Tab::Size:
      if (row != currentSizeIndex_) {
        applySize(row);
        SETTINGS.saveToFile();
        requestUpdate();
      }
      break;
    case Tab::Layout:
      confirmLayoutRow(row);
      break;
    case Tab::Style:
      confirmStyleRow(row);
      break;
    default:
      break;
  }
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font
// file, which frees and replaces the SdCardFont the render task may be reading.
void TextSettingsActivity::applySize(int listIndex) {
  RenderLock lock;
  currentSizeIndex_ = listIndex;
  fontdoc::apCo(renderer, sizes_[listIndex].pointSize);
}

void TextSettingsActivity::confirmLayoutRow(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::ParaIndent:
      SETTINGS.paragraphIndent = (SETTINGS.paragraphIndent + 1) % std::size(INDENT_IDS);
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::ParaSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::LineSpacing:
      optionPopup_.show(StrId::STR_LINE_SPACING, LINE_SPACING_IDS, static_cast<int>(std::size(LINE_SPACING_IDS)),
                        SETTINGS.lineSpacing, [](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::Alignment:
      optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                        SETTINGS.paragraphAlignment, [](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::ScreenMargin: {
      std::vector<std::string> options;
      options.reserve((MARGIN_MAX - MARGIN_MIN) / MARGIN_STEP + 1);
      for (int m = MARGIN_MIN; m <= MARGIN_MAX; m += MARGIN_STEP) options.push_back(std::to_string(m));
      const int cur = (std::clamp<int>(SETTINGS.screenMargin, MARGIN_MIN, MARGIN_MAX) - MARGIN_MIN) / MARGIN_STEP;
      optionPopup_.show(StrId::STR_SCREEN_MARGIN, options, cur, [](int idx) {
        SETTINGS.screenMargin = static_cast<uint8_t>(MARGIN_MIN + idx * MARGIN_STEP);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      break;
    }

    default:
      break;
  }
}

std::string TextSettingsActivity::layoutValueText(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::ParaIndent:
      return I18N.get(INDENT_IDS[SETTINGS.paragraphIndent < std::size(INDENT_IDS) ? SETTINGS.paragraphIndent : 0]);
    case LayoutRow::LineSpacing: {
      const uint8_t v = SETTINGS.lineSpacing;
      return v < std::size(LINE_SPACING_IDS) ? I18N.get(LINE_SPACING_IDS[v]) : I18N.get(StrId::STR_NORMAL);
    }
    case LayoutRow::ParaSpacing:
      return SETTINGS.extraParagraphSpacing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);

    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::InkWeight: {
      const uint8_t mask = sdFontSystem.availableWeightMask();
      const uint8_t current = SETTINGS.readerInkWeight;
      uint8_t next = current;
      for (int step = 1; step <= 3; ++step) {
        const auto candidate = static_cast<uint8_t>((current + step) % 3);
        if (mask & (1u << candidate)) {
          next = candidate;
          break;
        }
      }
      if (next == current) return;
      {
        RenderLock lock;
        SETTINGS.readerInkWeight = next;
        sdFontSystem.ensureLoaded(renderer);
      }
      break;
    }
    case StyleRow::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case StyleRow::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case StyleRow::EmbeddedStyle:
      SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
      break;
    case StyleRow::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;

    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string TextSettingsActivity::styleValueText(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::InkWeight:
      if (sdFontSystem.availableWeightMask() == 1) return tr(STR_INK_UNAVAILABLE);
      return I18N.get(INK_WEIGHT_IDS[SETTINGS.readerInkWeight <= 2 ? SETTINGS.readerInkWeight : 0]);
    case StyleRow::FocusReading:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

// Only Focus Reading shows in the preview (bold prefixes); the other Style rows
// have no distinct preview.
bool TextSettingsActivity::focusedRowHasNoPreview() const {
  if (ringPos() == 0 || tab_ != Tab::Style) return false;
  const StyleRow row = static_cast<StyleRow>(ringPos() - 1);
  return row == StyleRow::Hyphenation || row == StyleRow::EmbeddedStyle || row == StyleRow::AntiAliasing;
}

void TextSettingsActivity::switchTab(const int direction) {
  tab_ = static_cast<Tab>(adjacentTab(direction));
  rebuildRowItems();
  auto& n = activeNav();
  if (!mappedInput.hasTouch() && listCount() > 0 && n.selected <= 0) n.selected = 1;
  n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
  requestUpdate();
}

int TextSettingsActivity::listCount() const {
  switch (tab_) {
    case Tab::Family:
      return static_cast<int>(fonts_.size());
    case Tab::Size:
      return static_cast<int>(sizes_.size());
    case Tab::Layout:
      return static_cast<int>(LayoutRow::Count);
    case Tab::Style:
      return static_cast<int>(StyleRow::Count);

    default:
      return 0;
  }
}

std::string TextSettingsActivity::favoriteKey(int row) const {
  if (row < 0 || row >= listCount()) return {};
  return menufavorites::keyFor("text", activeTab(), tab_ == Tab::Family || tab_ == Tab::Size ? 0 : row);
}
int TextSettingsActivity::focusFavorite(const std::string& key) {
  const auto* route = menufavorites::find(key);
  if (!route || std::string(route->screen) != "text") return -1;
  onTabAction(route->tab);
  if (tab_ == Tab::Family || tab_ == Tab::Size) {
    RenderLock lock(*this);
    activeNav().selected = (tab_ == Tab::Family ? currentFamilyIndex_ : currentSizeIndex_) + 1;
    activeNav().followOnBuild = true;
    return ringPos() - 1;
  }
  for (int row = 0; row < listCount(); ++row)
    if (favoriteKey(row) == key) {
      RenderLock lock(*this);
      activeNav().selected = row + 1;
      activeNav().followOnBuild = true;
      return row;
    }
  return -1;
}
