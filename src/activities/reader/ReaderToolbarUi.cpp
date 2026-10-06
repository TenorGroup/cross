#include "ReaderToolbarUi.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "components/TenorMenuChrome.h"
#include "components/icons/readerToolbarIcons.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DISMISS = 1;  // tap anywhere on the page above the sheet
constexpr fui::ActionId ACTION_TOOL = 2;     // value = 0 Contents, 1 Text, 2 More
constexpr fui::ActionId ACTION_PREV = 3;     // scrub row: previous chapter
constexpr fui::ActionId ACTION_NEXT = 4;     // scrub row: next chapter
constexpr fui::ActionId ACTION_SCRUB = 5;    // progress track: dragPermille along the book
constexpr fui::ActionId ACTION_ROW = 6;      // panel list row, value = row index
constexpr fui::ActionId ACTION_CHOICE = 7;   // a value icon on a row, value = row * kChoiceStride + place
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
constexpr fui::ActionId ACTION_SIZE_STEP = 8;
constexpr fui::ActionId ACTION_SIZE_ENTRY = 9;
constexpr fui::ActionId ACTION_SPACING = 10;
constexpr fui::ActionId ACTION_NUMERIC = 12;
#endif

// Scrub row: two small round-cornered chapter buttons flanking a thin progress
// track with a round knob -- the reading page's chrome is light, so the
// controls stay slim rather than control-center sized.
constexpr int16_t kScrubButton = 36;  // chapter step buttons (square)
constexpr int16_t kScrubKnob = 16;    // round knob on the 2px progress track
constexpr int16_t kScrubGap = 12;     // air between the buttons and the track
// Tool row: a 24px glyph centred in each slot, the active slot in an outline
// pill. The whole slot is the tap target; the row height sets its size.
constexpr int16_t kToolRowH = 80;
constexpr int16_t kToolPillInset = 10;
constexpr int kToolCount = 3;
// Bottom sheet height for the panels. ListNav fits whole rows in the remaining
// list area; any spare pixels stay between the list and the switcher.
constexpr int kPanelHeightPercent = 62;
// Cap the sheet may grow to when rounding the list area up to a whole row.
constexpr int kPanelHeightMaxPercent = 72;
// Landscape has less vertical room; leave a narrow page strip for tap-to-dismiss.
constexpr int kLandscapePanelHeightPercent = 88;
}  // namespace

ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& renderer) : UiAppHost(renderer), renderer_(&renderer) {}

void ReaderToolbarUi::begin() {
  resetUi();
  pending_ = Routed{};
  nav_.reset();
  app.setScreen(&ReaderToolbarUi::screenFn, this);
}

void ReaderToolbarUi::render() {
  const bool handwritten = shell::isUgly();
  uiTarget.setPaintingEnabled(!handwritten);
  renderUi();
  // Wrapped rows can fit fewer than the fixed-height estimate; the nav then
  // advances the viewport after layout and asks for a rebuild. Converges (top
  // only moves forward toward the selection); the bound is a backstop.
  for (int pass = 0; pass < 3 && nav_.consumeRebuildNeeded(); ++pass) renderUi();
  uiTarget.setPaintingEnabled(true);
  if (handwritten) paintUgly();
}

ReaderToolbarUi::Routed ReaderToolbarUi::route(const MappedInputManager& input) {
  pending_ = Routed{};
  // routeHeld: the scrub track is a drag target, so held frames must reach it.
  // X4 Pro: a panel row takes a long press (pin to Favorites).
  const auto touch = routeTouch(input, /*withLongPress=*/tenorchrome::kTouchShell, /*routeHeld=*/true);
  if (touch.event) onAction(touch.event, this);
  pending_.routed = touch.routed;
  pending_.x = touch.snap.touchX;
  pending_.y = touch.snap.touchY;
  // Only the release commits a scrub: every held frame dispatches too
  // (dragPermille set), and re-paginating a chapter per frame would be seconds
  // of work per swipe.
  if (pending_.event == Event::Scrub && !touch.snap.touchReleased) pending_.event = Event::None;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (pending_.event == Event::SpacingDraft && touch.snap.touchReleased) pending_.event = Event::SpacingCommit;
#endif
  return pending_;
}

void ReaderToolbarUi::onAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ReaderToolbarUi*>(user);
  Routed& out = self->pending_;
  out.value = event.value;
  out.permille = event.dragPermille;
  out.hold = event.longPress;
  if (event.action >= ACTION_DISMISS && event.action <= ACTION_CHOICE) out.event = static_cast<Event>(event.action);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (event.action >= ACTION_SIZE_STEP && event.action <= ACTION_NUMERIC) out.event = static_cast<Event>(event.action);
#endif
  if (out.event == Event::Scrub && event.dragPermille < 0) out.event = Event::None;
  // A handled action repaints through the reader's own fast path, not through
  // app.invalidate(): the page underneath is the reader's to draw.
  self->app.clearTapFlash();
}

void ReaderToolbarUi::screenFn(UiScreen& screen, void* user) {
  auto* self = static_cast<ReaderToolbarUi*>(user);
  if (self->model_.panel) {
    self->buildPanel(screen);
  } else {
    self->buildToolbar(screen);
  }
}

void ReaderToolbarUi::buildSheet(UiScreen& screen, const fui::SheetProps& props, const int16_t height) {
  screen.setContentMarginFromScreen(model_.footerInsets);
  const auto bounds = screen.body();
  const auto sheetHeight = std::min(height, bounds.height);
  const fui::Rect rect{bounds.x, static_cast<int16_t>(bounds.bottom() - sheetHeight), bounds.width, sheetHeight};
  auto themed = props;
  if (themed.radius == fui::RADIUS_INHERIT) themed.radius = screen.theme().sheetRadius;
  fui::sheet(screen.frame(), rect, themed);
  skinFrame_ = rect;
  const auto content = fui::sheetContentRect(rect, themed);
  screen.insetContent(fui::Insets{static_cast<int16_t>(content.y - bounds.y), 0,
                                static_cast<int16_t>(bounds.bottom() - content.bottom()), 0});
}

// The Contents / Text / More row: three equal slots, an icon centred in each,
// the active one inside an outline pill (the theme's control radius). Each
// slot is registered as one tap target, so the row stays light (no filled
// tiles, no labels -- the glyphs carry the meaning).
void ReaderToolbarUi::buildToolRow(UiScreen& screen, const fui::LayoutAnchor anchor, const int16_t sideInset) {
  const auto& tokens = screen.theme();
  const fui::BitmapRef icons[kToolCount] = {fui::bitmapFromIcon(icon_reader_contents_24),
                                            fui::bitmapFromIcon(icon_reader_text_24),
                                            fui::bitmapFromIcon(icon_reader_more_24)};
  // sideInset absorbs the difference between the two hosts' content bands
  // (the toolbar's is spaceLg-inset, the panel's is full width): the slots
  // must land on the same x either way, or the icons jump when a tap swaps
  // the toolbar for a panel.
  const fui::Rect row = screen.take(anchor, kToolRowH).inset(fui::Insets{0, sideInset, 0, sideInset});
  const int16_t slotW = static_cast<int16_t>(row.width / kToolCount);
  // Theme radius as-is (the frontlight panel pattern); the fill clamps to
  // the shape's own height so round themes cannot overshoot.
  const uint8_t pillRadius = tokens.controlRadius;
  for (int i = 0; i < kToolCount; ++i) {
    const fui::Rect slot{static_cast<int16_t>(row.x + slotW * i), row.y, slotW, row.height};
    if (i == model_.activeTool) {
      screen.target().stroke(slot.inset(fui::Insets{4, kToolPillInset, 4, kToolPillInset}),
                             fui::Paint::solid(fui::Color::Black), 2, pillRadius);
    }
    const fui::Rect iconRect{static_cast<int16_t>(slot.x + (slot.width - 24) / 2),
                             static_cast<int16_t>(slot.y + (slot.height - 24) / 2), 24, 24};
    screen.target().bitmap(iconRect, icons[i], fui::BitmapMode::Center);
    screen.frame().hit(slot, ACTION_TOOL, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void ReaderToolbarUi::buildToolbar(UiScreen& screen) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  buildX4Toolbar(screen);
  return;
#endif
  const auto& tokens = screen.theme();

  // Sheet height from its content: scrub row, meta line, tool row, and the
  // air between them (a full spaceLg under the scrub row, so the progress
  // track and its chapter buttons don't crowd the meta line).
  const int16_t metaH = screen.target().lineHeight(tokens.smallText.font);
  const int16_t contentH = static_cast<int16_t>(tokens.spaceMd + kScrubButton + tokens.spaceLg + metaH +
                                                tokens.spaceSm + kToolRowH + tokens.spaceSm);
  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;
  // Grabber air matches the frontlight panel's card language (spaceLg around
  // the grabber, spaceMd more toward the free edge) so the two sheets read as
  // the same family.
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  buildSheet(screen, sheetProps, static_cast<int16_t>(contentH + grabberBand));
  screen.insetContent(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
  screen.spacer(tokens.spaceMd);

  // Scrub row: < [progress track + knob: tap/drag to jump] >
  {
    const fui::Rect band = screen.takeTop(kScrubButton, tokens.spaceLg);
    stepProps_.label = nullptr;
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_back_24);
    stepProps_.action = ACTION_PREV;
    stepProps_.inputMask = fui::InputTouch;
    stepProps_.styles.explicitlySet = true;
    stepProps_.styles.normal.background = fui::Paint::solid(fui::Color::White);
    stepProps_.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.borderWidth = 1;
    stepProps_.styles.normal.radius = tokens.controlRadius;
    stepProps_.styles.selected = stepProps_.styles.normal;
    stepProps_.styles.focused = stepProps_.styles.normal;
    stepProps_.styles.disabled = stepProps_.styles.normal;
    stepProps_.styles.active = stepProps_.styles.normal;
    stepProps_.styles.active.background = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.active.foreground = fui::Paint::solid(fui::Color::White);
    screen.button(stepProps_, fui::Rect{band.x, band.y, kScrubButton, kScrubButton});
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_next_24);
    stepProps_.action = ACTION_NEXT;
    screen.button(stepProps_,
                  fui::Rect{static_cast<int16_t>(band.right() - kScrubButton), band.y, kScrubButton, kScrubButton});

    // Progress: a 2px track with a solid round knob at the book position (the
    // fill edge IS the handle, so nothing else is drawn), the scrub hit rect
    // spanning the whole band height so a finger a little off the line still
    // scrubs. Tap-to-jump / drag arrive as dragPermille along this rect.
    const int16_t trackX = static_cast<int16_t>(band.x + kScrubButton + kScrubGap);
    const int16_t trackW = static_cast<int16_t>(band.width - 2 * (kScrubButton + kScrubGap));
    const int16_t trackY = static_cast<int16_t>(band.y + band.height / 2);
    const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
    screen.target().fill(fui::Rect{trackX, static_cast<int16_t>(trackY - 1), trackW, 2}, ink);
    const int permille = std::clamp(model_.progressPermille, 0, 1000);
    const int16_t knobX = static_cast<int16_t>(trackX + (static_cast<int32_t>(trackW - 1) * permille) / 1000);
    screen.target().fill(fui::Rect{static_cast<int16_t>(knobX - kScrubKnob / 2),
                                   static_cast<int16_t>(trackY - kScrubKnob / 2), kScrubKnob, kScrubKnob},
                         ink, static_cast<uint8_t>(kScrubKnob / 2));
    screen.frame().hit(fui::Rect{trackX, band.y, trackW, band.height}, ACTION_SCRUB, 0,
                       fui::InputTouch | fui::InputDrag);
  }

  // Meta line: chapter title (left), chapter page / book percent (right).
  {
    const fui::Rect line = screen.takeTop(metaH, tokens.spaceSm);
    skinMeta_ = line;
    fui::TextStyle titleStyle = tokens.smallText;
    titleStyle.bold = true;
    fui::TextStyle infoStyle = tokens.smallText;
    infoStyle.align = fui::TextAlign::Right;
    const int16_t infoW =
        model_.pageInfo ? screen.target().measureText(infoStyle.font, model_.pageInfo, infoStyle).width : 0;
    const fui::Rect titleRect{line.x, line.y, static_cast<int16_t>(line.width - infoW - tokens.spaceMd), line.height};
    if (model_.chapterTitle) screen.target().text(titleRect, model_.chapterTitle, titleStyle);
    if (model_.pageInfo) screen.target().text(line, model_.pageInfo, infoStyle);
  }

  buildToolRow(screen, fui::LayoutAnchor::Top, 0);  // content band already spaceLg-inset
}

// The row in use: its name in bold where the list left it blank, and a tick at the row end.
void ReaderToolbarUi::drawMarkedRows(UiScreen& screen, const fui::Rect& listRect, const int16_t rowH,
                                     const int16_t rowGap, const int windowCount) {
  const auto& tokens = screen.theme();
  fui::TextStyle bold = tokens.bodyText;
  bold.bold = true;
  const int16_t lineH = screen.target().lineHeight(bold.font);
  const fui::BitmapRef tick = fui::bitmapFromIcon(icon_reader_tick_24);
  for (int i = 0; i < windowCount; ++i) {
    if (!markedLabels_[i]) continue;
    const int16_t y = static_cast<int16_t>(listRect.y + i * (rowH + rowGap));
    const int16_t left = static_cast<int16_t>(listRect.x + listProps_.rowInset + listProps_.sidePadding);
    const int16_t right = static_cast<int16_t>(listRect.right() - listProps_.rowInset - listProps_.sidePadding);
    screen.target().text(fui::Rect{left, static_cast<int16_t>(y + (rowH - lineH) / 2),
                                   static_cast<int16_t>(right - left - 24 - tokens.spaceSm), lineH},
                         windowLabels_[i].c_str(), bold);
    screen.target().bitmap(fui::Rect{static_cast<int16_t>(right - 24), static_cast<int16_t>(y + (rowH - 24) / 2), 24, 24},
                           tick, fui::BitmapMode::Center);
  }
}

// A row's few values as icons along its right side, the one in use in an outline pill. Registered
// after the list, so on touch boards a tap on an icon is the value and not the row.
void ReaderToolbarUi::drawChoices(UiScreen& screen, const fui::Rect& listRect, const int16_t rowH,
                                  const int16_t rowGap, const int windowCount) {
  if (!model_.choiceCount || !model_.choiceIcon) return;
  const auto& tokens = screen.theme();
  for (int i = 0; i < windowCount; ++i) {
    const int index = nav_.top + i;
    const int count = std::min(model_.choiceCount(index), kChoiceStride);
    if (count <= 0) continue;
    const int inUse = model_.choiceInUse ? model_.choiceInUse(index) : -1;
    const int16_t y = static_cast<int16_t>(listRect.y + i * (rowH + rowGap));
    const int16_t right = static_cast<int16_t>(listRect.right() - listProps_.rowInset - listProps_.sidePadding);
    int16_t kChoiceW = 48;
    if (model_.denseRows) {
      const int16_t labelLeft = static_cast<int16_t>(listRect.x + listProps_.rowInset + listProps_.sidePadding);
      fui::TextStyle labelStyle = listProps_.labelText;
      labelStyle.maxLines = 1;
      const int16_t labelWidth = screen.target().measureText(labelStyle.font, windowLabels_[i].c_str(), labelStyle).width;
      const int16_t labelH = screen.target().lineHeight(labelStyle.font);
      kChoiceW = static_cast<int16_t>(std::clamp((right - labelLeft - labelWidth - tokens.spaceSm) / count, 32, 48));
      // The renderer truncates long labels to this band, including larger UI text and other locales.
      screen.target().text(fui::Rect{labelLeft, static_cast<int16_t>(y + (rowH - labelH) / 2),
                                     static_cast<int16_t>(right - count * kChoiceW - tokens.spaceSm - labelLeft), labelH},
                           windowLabels_[i].c_str(), labelStyle);
    }
    const int16_t left = static_cast<int16_t>(right - count * kChoiceW);
    skinChoices_[i] = {left, y, kChoiceW, rowH};
    for (int k = 0; k < count; ++k) {
      const fui::Rect cell{static_cast<int16_t>(left + k * kChoiceW), y, kChoiceW, rowH};
      if (k == inUse) {
        screen.target().stroke(cell.inset(fui::Insets{4, 3, 4, 3}), fui::Paint::solid(fui::Color::Black), 2,
                               tokens.controlRadius);
      }
      if (const freeink::Icon* icon = model_.choiceIcon(index, k)) {
        screen.target().bitmap(fui::Rect{static_cast<int16_t>(cell.x + (kChoiceW - 24) / 2),
                                         static_cast<int16_t>(y + (rowH - 24) / 2), 24, 24},
                               fui::bitmapFromIcon(*icon), fui::BitmapMode::Center);
      }
      if (!model_.denseRows) {
        screen.frame().hit(cell, ACTION_CHOICE, static_cast<int16_t>(index * kChoiceStride + k), fui::InputTouch);
      }
    }
  }
}

void ReaderToolbarUi::buildPanel(UiScreen& screen) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  buildX4Panel(screen);
  return;
#endif
  const auto& tokens = screen.theme();
  screen.setContentMarginFromScreen(model_.footerInsets);
  const fui::Rect safe = screen.body();

  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;  // tap the page above the sheet = back to the toolbar
  // Same grabber air as the toolbar sheet / frontlight panel.
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);

  // Size the sheet to an exact number of list rows instead of a fixed screen
  // share: a percentage leaves up to a row's height of dead space above the
  // switcher, and a short list (Text, More) leaves empty row slots. Chrome =
  // everything in the sheet that is not the list; the row count starts from
  // the target share, grows one row when that still fits the cap, and shrinks
  // to the item count when the list is shorter than the space.
  const int16_t titleH = screen.target().lineHeight(tokens.titleText.font);
  const int16_t rowH =
      model_.denseRows ? static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight) : tokens.rowHeight;
  const int16_t rowGap = model_.denseRows ? tokens.listRowGap : std::max(tokens.listRowGap, tokens.listTouchRowGap);
  const int16_t rowStride = static_cast<int16_t>(rowH + rowGap);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  const int16_t chrome =
      static_cast<int16_t>(grabberBand + titleH + tokens.spaceMd + tokens.spaceSm + kToolRowH + tokens.spaceSm);
  const bool landscape = safe.width > safe.height;
  const int16_t target =
      static_cast<int16_t>((safe.height * (landscape ? kLandscapePanelHeightPercent : kPanelHeightPercent)) / 100);
  const int16_t cap =
      static_cast<int16_t>((safe.height * (landscape ? kLandscapePanelHeightPercent : kPanelHeightMaxPercent)) / 100);
  int sheetRows = (target - chrome + rowGap) / rowStride;
  if (static_cast<int16_t>(chrome + (sheetRows + 1) * rowStride - rowGap) <= cap) ++sheetRows;
  if (model_.itemCount > 0 && sheetRows > model_.itemCount) sheetRows = model_.itemCount;
  if (model_.sheetRows > 0) sheetRows = model_.sheetRows;
  sheetRows = std::min(sheetRows, std::max(1, (safe.height - chrome + rowGap) / rowStride));
  if (sheetRows < 1) sheetRows = 1;
  buildSheet(screen, sheetProps, static_cast<int16_t>(chrome + sheetRows * rowStride - rowGap));
  // No blanket side inset: Screen::list() draws in the content band, and the
  // scroll track must reach the sheet's edge like a full-screen list's does.
  // The title insets itself; the rows inset via rowInset below.

  // Title line: panel name left, page position right when the list spans pages.
  {
    fui::TextStyle titleStyle = tokens.titleText;
    titleStyle.bold = true;
    const fui::Rect line =
        screen.takeTop(titleH, tokens.spaceMd).inset(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
    screen.target().text(line, model_.panelTitle, titleStyle);
    // Filled in below once the viewport is known; reserve the rect now.
    pageIndicatorRect_ = line;
  }

  // Switcher row along the sheet's bottom edge; the list takes what is left.
  screen.spacer(tokens.spaceSm, fui::LayoutAnchor::Bottom);
  buildToolRow(screen, fui::LayoutAnchor::Bottom, tokens.spaceLg);  // full-width band
  screen.spacer(tokens.spaceSm, fui::LayoutAnchor::Bottom);

  listProps_.count = static_cast<uint16_t>(std::max(0, model_.itemCount));
  listProps_.action = ACTION_ROW;
  listProps_.inputMask = fui::InputTouch;  // physical buttons stay with the reader
  listProps_.rowHeight = rowH;
  listProps_.rowGap = rowGap;
  // Body-size text: small reads condensed and the taller row doubles as the
  // tap target. A little air inside the row so the cursor's outline does not
  // cut the first letter, the value or the chevron/tick at the row end.
  listProps_.labelText = tokens.bodyText;
  listProps_.sidePadding = tokens.spaceMd;
  // The list band spans the sheet's full width -- the scroll track hugs the
  // panel edge (theme bezel inset included) exactly like a full-screen list.
  // rowInset pulls the rows back to the title's spaceLg alignment.
  listProps_.rowInset = tokens.spaceLg;
  const fui::Rect listRect = screen.body();
  skinList_ = listRect;
  const int count = std::max(0, model_.itemCount);
  // The nav owns selection + viewport (same fui::ListNav idiom as the list
  // menu screens). A shown cursor re-follows into view on every build; a
  // hidden one (-1, touch) leaves the viewport where scrolling put it.
  nav_.selected = std::clamp(model_.selectedIndex, -1, count - 1);
  nav_.followOnBuild = nav_.selected >= 0;
  nav_.followPending = false;
  nav_.syncToProps(listRect, listProps_.rowHeight, rowGap, count, listProps_);

  // Materialise only the visible window of rows.
  const int windowCount = std::min({nav_.visibleRows, count - nav_.top, kMaxWindow});
  for (int i = 0; i < windowCount; ++i) {
    const int index = nav_.top + i;
    windowLabels_[i] = model_.rowText ? model_.rowText(index) : std::string();
    markedLabels_[i] = model_.rowMarked && model_.rowMarked(index);
    windowValues_[i] = model_.rowValue ? model_.rowValue(index) : std::string();
    const bool choices = model_.choiceCount && model_.choiceCount(index) > 0;
    if (choices) windowValues_[i].clear();  // drawn by drawChoices
    fui::ListItem item;
    item.label = markedLabels_[i] || (choices && model_.denseRows) ? "" : windowLabels_[i].c_str();
    item.value = windowValues_[i].empty() ? nullptr : windowValues_[i].c_str();
    item.actionValue = static_cast<int16_t>(index);
    windowItems_[i] = item;
  }
  listProps_.items = windowItems_;
  listProps_.itemsWindowFirst = static_cast<uint16_t>(nav_.top);
  listProps_.itemsWindowCount = static_cast<uint16_t>(std::max(0, windowCount));
  listProps_.valueText = tokens.bodyText;
  listProps_.valueText.bold = true;
  if (count > 0) {
    screen.list(listProps_);
    drawMarkedRows(screen, listRect, rowH, rowGap, windowCount);
    drawChoices(screen, listRect, rowH, rowGap, windowCount);
  }

  const int pageRows = nav_.pageRows();
  const int totalPages = pageRows > 0 ? (count + pageRows - 1) / pageRows : 0;
  if (totalPages > 1) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", nav_.top / pageRows + 1, totalPages);
    fui::TextStyle pageStyle = tokens.smallText;
    pageStyle.align = fui::TextAlign::Right;
    screen.target().text(pageIndicatorRect_, buf, pageStyle);
  }
}

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
namespace {
fui::Rect readerFrame(const fui::Rect& screen, const int height) {
  return {16, static_cast<int16_t>(tenorchrome::footBackTop(screen.height) - 12 - height),
          static_cast<int16_t>(screen.width - 32), static_cast<int16_t>(height)};
}
}

int ReaderToolbarUi::scrollRows(const MappedInputManager& input, const int count) const {
  return swipeRows(input, nav_, count, ACTION_ROW);
}

void ReaderToolbarUi::buildX4Tools(UiScreen& screen) {
  if (model_.textView == TextView::PointSize) return;
  const auto bounds = screen.frame().screen();
  for (int i = 0; i < tenorchrome::READER_TOOLS; ++i) {
    const auto r = tenorchrome::readerToolRect(bounds.width, bounds.height, i);
    screen.frame().hit({static_cast<int16_t>(r.x), static_cast<int16_t>(r.y),
                        static_cast<int16_t>(r.width), static_cast<int16_t>(r.height)},
                       ACTION_TOOL, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void ReaderToolbarUi::buildX4Toolbar(UiScreen& screen) {
  const auto& tokens = screen.theme();
  const auto bounds = screen.frame().screen();
  const auto frame = readerFrame(bounds, 116);
  skinFrame_ = frame;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  screen.target().fill(frame, fui::Paint::solid(fui::Color::White), 20);
  if (uiTarget.paintingEnabled() && renderer_) tenorchrome::drawPanel(*renderer_, frame.y, frame.height);
  screen.frame().hit({0, 0, bounds.width, frame.y}, ACTION_DISMISS, 0, fui::InputTouch);
  stepProps_ = fui::ButtonProps{};
  stepProps_.inputMask = fui::InputTouch;
  stepProps_.minTouchSize = 60;
  stepProps_.icon = fui::bitmapFromIcon(icon_reader_back_24);
  stepProps_.action = ACTION_PREV;
  screen.button(stepProps_, {static_cast<int16_t>(frame.x + 8), static_cast<int16_t>(frame.y + 8), 60, 60});
  stepProps_.icon = fui::bitmapFromIcon(icon_reader_next_24);
  stepProps_.action = ACTION_NEXT;
  screen.button(stepProps_, {static_cast<int16_t>(frame.right() - 68), static_cast<int16_t>(frame.y + 8), 60, 60});
  const fui::Rect track{static_cast<int16_t>(frame.x + 76), static_cast<int16_t>(frame.y + 8),
                        static_cast<int16_t>(frame.width - 152), 60};
  const int16_t cy = static_cast<int16_t>(track.y + 30);
  screen.target().fill({track.x, cy, track.width, 2}, ink);
  const int16_t kx = static_cast<int16_t>(track.x + (track.width - 1) * std::clamp(model_.progressPermille, 0, 1000) / 1000);
  screen.target().fill({static_cast<int16_t>(kx - 8), static_cast<int16_t>(cy - 7), 16, 16}, ink, 8);
  screen.frame().hit(track, ACTION_SCRUB, 0, fui::InputTouch | fui::InputDrag);
  fui::TextStyle style = tokens.smallText;
  style.maxLines = 1;
  const int16_t lineH = screen.target().lineHeight(style.font);
  const fui::Rect meta{static_cast<int16_t>(frame.x + 16), static_cast<int16_t>(frame.bottom() - lineH - 12),
                       static_cast<int16_t>(frame.width - 32), lineH};
  skinMeta_ = meta;
  style.align = fui::TextAlign::Right;
  const int16_t infoW = model_.pageInfo ? screen.target().measureText(style.font, model_.pageInfo, style).width : 0;
  if (model_.pageInfo) screen.target().text(meta, model_.pageInfo, style);
  style.align = fui::TextAlign::Left;
  if (model_.chapterTitle) screen.target().text({meta.x, meta.y, static_cast<int16_t>(std::max(0, meta.width - infoW - 12)), lineH}, model_.chapterTitle, style);
  buildX4Tools(screen);
}

void ReaderToolbarUi::buildX4Panel(UiScreen& screen) {
  const auto& tokens = screen.theme();
  const auto bounds = screen.frame().screen();
  const auto frame = readerFrame(bounds, 350);
  skinFrame_ = frame;
  screen.target().fill(frame, fui::Paint::solid(fui::Color::White), 20);
  screen.frame().hit({0, 0, bounds.width, frame.y}, ACTION_DISMISS, 0, fui::InputTouch);
  fui::TextStyle header = tokens.smallText;
  header.bold = true;
  header.maxLines = 1;
  screen.target().text({static_cast<int16_t>(frame.x + 16), static_cast<int16_t>(frame.y + 4),
                        static_cast<int16_t>(frame.width - 32), 36}, model_.panelTitle, header);
  if (model_.textView == TextView::Spacing) {
    buildX4Spacing(screen, frame);
  } else if (model_.textView == TextView::PointSize) {
    buildX4Keypad(screen, frame);
  } else {
    const bool rows = model_.textView == TextView::Rows;
    const bool fonts = model_.textView == TextView::Fonts;
    const fui::Rect listRect{frame.x, static_cast<int16_t>(frame.y + 40), frame.width,
                            static_cast<int16_t>(fonts ? 286 : 310)};
    skinList_ = listRect;
    const int count = std::max(0, model_.itemCount);
    listProps_ = fui::ListProps{};
    listProps_.count = static_cast<uint16_t>(count);
    listProps_.action = ACTION_ROW;
    listProps_.inputMask = fui::InputTouch | fui::InputLongPress;
    listProps_.rowHeight = 62;
    listProps_.rowGap = 0;
    listProps_.sidePadding = 16;
    listProps_.rowInset = 0;
    // Names start at the frame's text edge (16 px in, as on the Settings rows), values end 16 px from its
    // right side: a name centred in the room its value left wandered from row to row.
    listProps_.centerSingleLine = false;
    listProps_.labelText = tokens.bodyText;
    listProps_.labelText.maxLines = 1;
    listProps_.valueText = tokens.smallText;
    listProps_.valueText.bold = true;
    listProps_.valueText.maxLines = 1;
    listProps_.chosenMark = fui::bitmapFromIcon(icon_reader_tick_24);
    listProps_.partialTrailingRow = fonts;
    listProps_.scrollIndicatorInset = 4;
    // The list ends at the frame's bottom edge: the bar keeps out of its round corner and rounds its ends.
    listProps_.scrollIndicatorWidth = 6;
    listProps_.scrollIndicatorFrameRadius = tenorchrome::PANEL_RADIUS;
    listProps_.rowStyles = fui::defaultListRowStyles();
    nav_.selected = std::clamp(model_.selectedIndex, -1, count - 1);
    nav_.followOnBuild = nav_.selected >= 0;
    nav_.followPending = false;
    nav_.syncToProps(listRect, 62, 0, count, listProps_);
    const int windowCount = std::min({nav_.visibleRows + (fonts ? 1 : 0), count - nav_.top, kMaxWindow});
    for (int i = 0; i < windowCount; ++i) {
      const int index = nav_.top + i;
      windowLabels_[i] = model_.rowText ? model_.rowText(index) : std::string();
      windowValues_[i] = model_.rowValue ? model_.rowValue(index) : std::string();
      fui::ListItem item;
      item.label = rows && index == 1 ? "" : windowLabels_[i].c_str();
      item.value = windowValues_[i].empty() || (rows && index == 1) ? nullptr : windowValues_[i].c_str();
      item.actionValue = static_cast<int16_t>(index);
      item.chosen = model_.rowMarked && model_.rowMarked(index);
      item.opensNext = rows && (index == 0 || index == 2);
      windowItems_[i] = item;
    }
    listProps_.items = windowItems_;
    listProps_.itemsWindowFirst = static_cast<uint16_t>(nav_.top);
    listProps_.itemsWindowCount = static_cast<uint16_t>(std::max(0, windowCount));
    if (count > 0) fui::list(screen.frame(), listRect, listProps_);
    if (model_.rowPinned && renderer_ && uiTarget.paintingEnabled()) {
      // The heart stands before the value (and the chevron), inside the frame; on the size row, before its "-".
      const int16_t lh = screen.target().lineHeight(listProps_.labelText.font);
      const int chevron = fui::listChevronWidth(fui::listChevronSpan(lh)) + listProps_.textGap;
      // The scroll bar's strip, which list() takes from the rows' right side (rowInset is 0 here).
      const bool reserved = listProps_.scrollIndicator && (count > nav_.visibleRows || listProps_.nav);
      const int strip = reserved ? listProps_.scrollIndicatorWidth + listProps_.scrollIndicatorInset + 2 : 0;
      for (int i = 0; i < std::min(nav_.visibleRows, windowCount); ++i) {
        const int index = nav_.top + i;
        if (!model_.rowPinned(index)) continue;
        const auto& item = windowItems_[i];
        int right = frame.right() - strip - listProps_.sidePadding - (item.opensNext ? chevron : 0);
        if (rows && index == 1)
          right = frame.right() - 204;
        else if (item.value)
          right -= screen.target().measureText(listProps_.valueText.font, item.value, listProps_.valueText).width;
        tenorchrome::drawFavoriteMark(*renderer_, right - 8 - tenorchrome::FAVORITE_MARK,
                                      listRect.y + i * 62 + (62 - tenorchrome::FAVORITE_MARK) / 2);
      }
    }
    if (count == 0 && model_.emptyText) {
      fui::TextStyle hint = tokens.bodyText;
      hint.align = fui::TextAlign::Center;
      hint.maxLines = 3;
      screen.target().text({static_cast<int16_t>(listRect.x + 32), static_cast<int16_t>(listRect.y + 40),
                            static_cast<int16_t>(listRect.width - 64), static_cast<int16_t>(listRect.height - 80)},
                           model_.emptyText, hint);
    }
    // The size row (index 1) with its stepper, where the scrolled rows put it: wholly in view only.
    const int sizeSlot = 1 - nav_.top;
    if (rows && sizeSlot >= 0 && sizeSlot < std::min(nav_.visibleRows, windowCount)) {
      const int16_t y = static_cast<int16_t>(listRect.y + sizeSlot * 62);
      fui::TextStyle label = tokens.bodyText;
      label.maxLines = 1;
      screen.target().text({static_cast<int16_t>(frame.x + 16), y, static_cast<int16_t>(frame.width - 236), 62},
                           windowLabels_[sizeSlot].c_str(), label);
      stepProps_ = fui::ButtonProps{};
      stepProps_.inputMask = fui::InputTouch;
      stepProps_.minTouchSize = 60;
      stepProps_.text = tokens.bodyText;
      stepProps_.action = ACTION_SIZE_STEP;
      stepProps_.label = "-";
      stepProps_.value = -1;
      screen.button(stepProps_, {static_cast<int16_t>(frame.right() - 204), y, 60, 62});
      stepProps_.label = windowValues_[sizeSlot].c_str();
      stepProps_.action = ACTION_SIZE_ENTRY;
      stepProps_.value = 0;
      screen.button(stepProps_, {static_cast<int16_t>(frame.right() - 140), y, 72, 62});
      stepProps_.label = "+";
      stepProps_.action = ACTION_SIZE_STEP;
      stepProps_.value = 1;
      screen.button(stepProps_, {static_cast<int16_t>(frame.right() - 64), y, 60, 62});
    }
  }
  if (uiTarget.paintingEnabled() && renderer_) tenorchrome::drawPanel(*renderer_, frame.y, frame.height);
  buildX4Tools(screen);
}

void ReaderToolbarUi::buildX4Spacing(UiScreen& screen, const fui::Rect& frame) {
  const auto& tokens = screen.theme();
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const int16_t left = static_cast<int16_t>(frame.x + 48);
  const int16_t width = static_cast<int16_t>(frame.width - 96);
  const int16_t y = static_cast<int16_t>(frame.y + 150);
  screen.target().fill({left, y, width, 2}, ink);
  for (int i = 0; i < 5; ++i) {
    const int16_t x = static_cast<int16_t>(left + (width - 1) * i / 4);
    screen.target().fill({x, static_cast<int16_t>(y - 10), 2, 22}, ink);
    fui::TextStyle style = tokens.smallText;
    style.bold = i == model_.spacingPlace;
    style.align = fui::TextAlign::Center;
    style.maxLines = 2;
    if (model_.spacingLabel) screen.target().text({static_cast<int16_t>(x - 44), static_cast<int16_t>(y + 24), 88, 100}, model_.spacingLabel(i), style);
  }
  const int16_t knobX = static_cast<int16_t>(left + (width - 1) * std::clamp(model_.spacingDraftPermille, 0, 1000) / 1000);
  screen.target().fill({static_cast<int16_t>(knobX - 10), static_cast<int16_t>(y - 9), 20, 20}, ink, 10);
  // One drag target spans the same endpoints as the ordinal ruler.
  screen.frame().hit({left, static_cast<int16_t>(y - 50), width, 100}, ACTION_SPACING, 0, fui::InputTouch | fui::InputDrag);
}

void ReaderToolbarUi::buildX4Keypad(UiScreen& screen, const fui::Rect& frame) {
  const auto& tokens = screen.theme();
  fui::TextStyle numeric = tokens.titleText;
  numeric.bold = true;
  numeric.align = fui::TextAlign::Center;
  screen.target().text({static_cast<int16_t>(frame.x + 16), static_cast<int16_t>(frame.y + 40), 104, 52}, model_.numericDraft, numeric);
  fui::TextStyle hint = tokens.smallText;
  hint.font = fui::GfxRendererTarget::FONT_LABEL;
  hint.align = fui::TextAlign::Center;
  hint.maxLines = 1;
  screen.target().text({static_cast<int16_t>(frame.x + 120), static_cast<int16_t>(frame.y + 40),
                        static_cast<int16_t>(frame.width - 136), 52}, model_.numericHint, hint);
  fui::KeyGridKey keys[12];
  static const char* digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
  for (int i = 0; i < 9; ++i) { keys[i].label = digits[i]; keys[i].value = i + 1; }
  keys[9].kind = fui::KeyKind::Delete; keys[9].value = 10;
  keys[10].label = digits[9]; keys[10].value = 0;
  keys[11].kind = fui::KeyKind::Ok; keys[11].label = tr(STR_DONE); keys[11].value = 11;
  static fui::KeyGridProps props;
  props = fui::KeyGridProps{};
  props.keys = keys;
  props.rows = 4; props.cols = 3;
  props.action = ACTION_NUMERIC; props.inputMask = fui::InputTouch;
  props.labelText = tokens.bodyText;
  props.minTouchSize = 60; props.gap = 4; props.radius = 8;
  fui::keyGrid(screen.frame(), {static_cast<int16_t>(frame.x + 8), static_cast<int16_t>(frame.y + 92),
                               static_cast<int16_t>(frame.width - 16), 252}, props);
}
#endif

void readerugly::text(const GfxRenderer& r, const fui::Rect& rect, const char* label, const fui::TextAlign align) {
  if (!label || !*label || rect.empty()) return;
  const auto clip = r.getClipRect();
  const int left = std::max<int>(rect.x, clip[0]), top = std::max<int>(rect.y, clip[1]);
  r.setClipRect(left, top, std::max(0, std::min<int>(rect.right(), clip[0] + clip[2]) - left),
                std::max(0, std::min<int>(rect.bottom(), clip[1] + clip[3]) - top));
  const auto fitted = ugly::fit(r, ugly::Size::S22, label, rect.width);
  const int width = ugly::width(r, ugly::Size::S22, fitted.c_str());
  const int x = align == fui::TextAlign::Center ? rect.x + (rect.width - width) / 2
                : align == fui::TextAlign::Right ? rect.right() - width : rect.x;
  ugly::text(r, ugly::Size::S22, x, rect.y + (rect.height + ugly::ascent(ugly::Size::S22)) / 2, fitted.c_str());
  r.setClipRect(clip[0], clip[1], clip[2], clip[3]);
}

void readerugly::paper(const GfxRenderer& r, const fui::Rect& rect) {
  r.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  ugly::line(r, rect.x + 2, rect.y + 3, rect.right() - 3, rect.y + 1, 810, 2);
  ugly::line(r, rect.right() - 3, rect.y + 1, rect.right() - 1, rect.bottom() - 3, 811, 2);
  ugly::line(r, rect.right() - 1, rect.bottom() - 3, rect.x + 3, rect.bottom() - 1, 812, 2);
  ugly::line(r, rect.x + 3, rect.bottom() - 1, rect.x + 2, rect.y + 3, 813, 2);
}

void readerugly::selected(const GfxRenderer& r, const fui::Rect& rect) {
  if (!rect.empty()) ugly::circle(r, ugly::Circle::Row,
      {rect.x + 5, rect.y + 5, rect.right() - 5, rect.bottom() - 5}, 0, 0, 2);
}

void ReaderToolbarUi::paintUgly() {
  auto& r = *renderer_;
  readerugly::paper(r, skinFrame_);
  if (!model_.panel) {
    for (const auto action : {ACTION_PREV, ACTION_NEXT}) {
      const auto box = app.publishedRect(action, 0);
      ugly::mark(r, action == ACTION_PREV ? ugly::Mark::Left : ugly::Mark::Right,
                 box.x + box.width / 2, box.y + box.height / 2);
      readerugly::selected(r, box);
    }
    const auto track = app.publishedRect(ACTION_SCRUB, 0);
    const int cy = track.y + track.height / 2;
    ugly::line(r, track.x, cy, track.right(), cy, 814, 2);
    const int kx = track.x + (track.width - 1) * std::clamp(model_.progressPermille, 0, 1000) / 1000;
    ugly::circle(r, ugly::Circle::Object, {kx - 6, cy - 6, kx + 6, cy + 6}, 0, 0, 2);
    const int infoWidth = model_.pageInfo ? ugly::width(r, ugly::Size::S22, model_.pageInfo) : 0;
    auto title = skinMeta_;
    title.width = static_cast<int16_t>(std::max(0, title.width - infoWidth - 12));
    readerugly::text(r, title, model_.chapterTitle);
    readerugly::text(r, skinMeta_, model_.pageInfo, fui::TextAlign::Right);
  } else {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    const fui::Rect header{static_cast<int16_t>(skinFrame_.x + 16), static_cast<int16_t>(skinFrame_.y + 4),
                           static_cast<int16_t>(skinFrame_.width - 32), 36};
#else
    auto header = pageIndicatorRect_;
    const int pageRows = nav_.pageRows();
    const int pages = pageRows > 0 ? (model_.itemCount + pageRows - 1) / pageRows : 0;
    if (pages > 1) {
      char page[16]; snprintf(page, sizeof(page), "%d/%d", nav_.top / pageRows + 1, pages);
      const int width = ugly::width(r, ugly::Size::S22, page);
      readerugly::text(r, header, page, fui::TextAlign::Right);
      header.width = static_cast<int16_t>(std::max(0, header.width - width - 12));
    }
#endif
    readerugly::text(r, header, model_.panelTitle);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    if (model_.textView == TextView::PointSize) {
      readerugly::text(r, {static_cast<int16_t>(skinFrame_.x + 16), static_cast<int16_t>(skinFrame_.y + 40), 104, 52},
                       model_.numericDraft, fui::TextAlign::Center);
      readerugly::text(r, {static_cast<int16_t>(skinFrame_.x + 120), static_cast<int16_t>(skinFrame_.y + 40),
                           static_cast<int16_t>(skinFrame_.width - 136), 52}, model_.numericHint, fui::TextAlign::Center);
      for (int value = 0; value < 12; ++value) {
        const auto box = app.publishedRect(ACTION_NUMERIC, value);
        if (value == 10) ugly::mark(r, ugly::Mark::Back, box.x + box.width / 2, box.y + box.height / 2);
        else {
          char number[4]; snprintf(number, sizeof(number), "%d", value);
          readerugly::text(r, box, value == 11 ? tr(STR_DONE) : number, fui::TextAlign::Center);
        }
      }
    } else if (model_.textView == TextView::Spacing) {
      const auto box = app.publishedRect(ACTION_SPACING, 0);
      const int cy = box.y + box.height / 2;
      ugly::line(r, box.x, cy, box.right(), cy, 815, 2);
      for (int i = 0; i < 5; ++i) {
        const int x = box.x + (box.width - 1) * i / 4;
        ugly::line(r, x, cy - 10, x, cy + 10, 816 + i, 2);
        const std::string label = model_.spacingLabel ? model_.spacingLabel(i) : "";
        const auto space = label.find(' ');
        if (space == std::string::npos) {
          readerugly::text(r, {static_cast<int16_t>(x - 44), static_cast<int16_t>(cy + 24), 88, 100},
                           label.c_str(), fui::TextAlign::Center);
        } else {
          readerugly::text(r, {static_cast<int16_t>(x - 44), static_cast<int16_t>(cy + 48), 88, 26},
                           label.substr(0, space).c_str(), fui::TextAlign::Center);
          readerugly::text(r, {static_cast<int16_t>(x - 44), static_cast<int16_t>(cy + 74), 88, 26},
                           label.substr(space + 1).c_str(), fui::TextAlign::Center);
        }
      }
      const int x = box.x + (box.width - 1) * std::clamp(model_.spacingDraftPermille, 0, 1000) / 1000;
      ugly::circle(r, ugly::Circle::Object, {x - 8, cy - 8, x + 8, cy + 8}, 0, 0, 2);
    } else
#endif
    {
      const auto clip = r.getClipRect();
      r.setClipRect(skinList_.x, skinList_.y, skinList_.width, skinList_.height);
      for (int index = nav_.top; index < std::min(model_.itemCount, nav_.top + kMaxWindow); ++index) {
        auto row = app.publishedRect(ACTION_ROW, index);
        // A font page retains the SDK's clipped next-row preview, with no extra hit target.
        if (row.empty() && listProps_.partialTrailingRow && index - nav_.top == nav_.visibleRows)
          row = {skinList_.x, static_cast<int16_t>(skinList_.y + (index - nav_.top) * (listProps_.rowHeight + listProps_.rowGap)),
                 skinList_.width, listProps_.rowHeight};
        if (row.empty()) continue;
        auto label = row.inset(fui::Insets{0, 16, 0, 16});
        const int i = index - nav_.top;
        const bool marked = model_.rowMarked && model_.rowMarked(index);
        const bool opensNext = windowItems_[i].opensNext;
        if (marked || opensNext) label.width = static_cast<int16_t>(std::max(0, label.width - 28));
        const auto& value = windowValues_[i];
        bool numericRow = false;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
        numericRow = model_.textView == TextView::Rows && index == 1;
#endif
        if (!value.empty() && !numericRow) {
          const int width = std::min<int>(label.width / 2, ugly::width(r, ugly::Size::S22, value.c_str()));
          readerugly::text(r, {static_cast<int16_t>(label.right() - width), label.y,
                               static_cast<int16_t>(width), label.height}, value.c_str(), fui::TextAlign::Right);
          label.width = static_cast<int16_t>(std::max(0, label.width - width - 12));
        }
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
        if (model_.textView == TextView::Rows && index == 1) {
          label.width = static_cast<int16_t>(skinFrame_.width - 236);
          for (const auto action : {ACTION_SIZE_STEP, ACTION_SIZE_ENTRY}) for (int v = -1; v <= 1; ++v) {
            const auto key = app.publishedRect(action, v);
            if (!key.empty()) readerugly::text(r, key, action == ACTION_SIZE_ENTRY ? value.c_str() : v < 0 ? "-" : "+", fui::TextAlign::Center);
          }
        }
#endif
        const int choices = model_.choiceCount ? std::min(model_.choiceCount(index), kChoiceStride) : 0;
        if (choices > 0) label.width = static_cast<int16_t>(std::max(0, skinChoices_[i].x - label.x - 8));
        readerugly::text(r, label, windowLabels_[i].c_str());
        if (model_.selectedIndex == index) readerugly::selected(r, row);
        if (marked) ugly::tick(r, row.right() - 30, row.y + row.height / 2);
        if (opensNext) ugly::mark(r, ugly::Mark::Right, row.right() - 24, row.y + row.height / 2);
        for (int k = 0; k < choices; ++k) {
          auto choice = skinChoices_[i];
          choice.x = static_cast<int16_t>(choice.x + k * choice.width);
          const int x = choice.x;
          if (model_.choiceIcon) if (const auto* icon = model_.choiceIcon(index, k))
            uiTarget.bitmap({static_cast<int16_t>(x + (choice.width - 24) / 2), static_cast<int16_t>(row.y + (row.height - 24) / 2), 24, 24},
                            fui::bitmapFromIcon(*icon), fui::BitmapMode::Center);
          if (model_.choiceInUse && model_.choiceInUse(index) == k) readerugly::selected(r, choice);
        }
      }
      if (model_.itemCount > nav_.visibleRows && skinList_.height > 0) {
        const int x = skinList_.right() - 4;
        ugly::line(r, x, skinList_.y + 4, x, skinList_.bottom() - 4, 825, 1);
        const int y = skinList_.y + skinList_.height * nav_.top / model_.itemCount;
        const int h = std::max(8, skinList_.height * nav_.visibleRows / model_.itemCount);
        ugly::line(r, x - 2, y, x - 2, std::min<int>(skinList_.bottom(), y + h), 826, 2);
      }
      r.setClipRect(clip[0], clip[1], clip[2], clip[3]);
    }
  }
  static constexpr StrId tools[] = {StrId::STR_TOOL_CONTENTS, StrId::STR_TOOL_TEXT, StrId::STR_TOOL_MORE};
  for (int tool = 0; tool < 3; ++tool) {
    const auto box = app.publishedRect(ACTION_TOOL, tool);
    if (!box.empty()) {
      readerugly::text(r, box.inset(fui::Insets{0, 8, 0, 8}), I18N.get(tools[tool]), fui::TextAlign::Center);
      if (model_.activeTool == tool) readerugly::selected(r, box);
    }
  }
}
