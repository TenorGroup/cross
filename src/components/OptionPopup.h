#pragma once
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <utility>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/OptionPopupLayout.h"
#include "components/PageScrollbar.h"
#include "components/TenorMenuChrome.h"
#include "components/UiAppHelpers.h"
#include "components/UiAppHost.h"
#include "components/TouchScroll.h"
#include "components/icons/tenorRowMarks.h"
#include "components/ContextMenuIcons.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyChrome.h"
#include "shells/ugly/UglyInk.h"

// Modal option picker drawn over the current screen (no clear) via
// fui::optionDialog. Touch hit-testing is the SDK's InteractionBuffer: each
// render registers the option buttons (plus a chrome guard rect) on the render
// task, and handleInput routes touch snapshots against that table on the loop
// task, gated by the uiReady handshake (same pattern as UiListActivity).
// render() builds into InteractionBuffer's non-published generation
// (beginPublishCycle()) and publishes it only once every hit() call for the
// frame is done (publish()), so handleInput()'s routePublished()/
// publishedData() reads on the loop task always see a complete table, never
// one render is mid-rebuilding. uiReady closes when show() replaces the
// popup's data, then stays open across ordinary repaints after the first
// publication so a release cannot be dropped during a highlight repaint.
class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    activate(currentIndex, std::move(onSelect));
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    headline.clear();
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    activate(currentIndex, std::move(onSelect));
  }

  // As above, plus a subject line inside the dialog (a book or event title).
  // It wraps to several lines under the caption; the dialog grows to fit.
  // Touch: a question, it stands over the bar at the foot (a row tapped on the screen before is not its own).
  void show(const char* titleStr, const char* headlineStr, const char* const* options, int optionCount,
            int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    headline = headlineStr ? headlineStr : "";
    hasAnchor = false;
    headLaid = false;
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings = options;
    activate(currentIndex, std::move(onSelect));
  }

  // Value choices use the dispatched row as anchor; action dialogs retain
  // the caller's frame bounds.
  void showInFrame(const freeink::ui::Rect& parentFrame, StrId titleId, const StrId* optionIds,
                   int optionCount, int currentIndex, std::function<void(int)> onSelect) {
    show(titleId, optionIds, optionCount, currentIndex, std::move(onSelect));
    activateInFrame(parentFrame);
  }

  void showInFrame(const freeink::ui::Rect& parentFrame, StrId titleId,
                   const std::vector<std::string>& options, int currentIndex,
                   std::function<void(int)> onSelect) {
    show(titleId, options, currentIndex, std::move(onSelect));
    activateInFrame(parentFrame);
  }

  void showInFrame(const freeink::ui::Rect& parentFrame, const char* titleStr,
                   const char* const* options, int optionCount, int currentIndex,
                   std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    activateInFrame(parentFrame);
  }

  static int rowsInFrame(const int frameHeight, const int headerHeight, const int rowHeight,
                         const int optionCount) {
    return optionPopupFrameRows(frameHeight, headerHeight, rowHeight, optionCount);
  }

  bool usesFrame() const { return inFrame; }
  freeink::ui::Rect activeFrame() const { return frameRect; }
  freeink::ui::Rect lastFrame() const { return lastRenderedFrame; }

  void alignValueTo(const freeink::ui::Rect& row) {
    anchor = row;
    anchor.width = std::max<int16_t>(0, anchor.width - 23);
    hasAnchor = row.height > 0;
    alignSelected = hasAnchor;
  }

  // Touch shell: the actions of a held row in a menu anchored to the row, no title: under it when the
  // row is in the upper half and the menu fits over the bar at the foot, else over it; left on the list's
  // margin. A tap outside closes it.
  void showAnchored(const freeink::ui::Rect& row, const StrId* optionIds, int optionCount,
                    std::function<void(int)> onSelect) {
    show(StrId::STR_NONE_OPT, optionIds, optionCount, 0, std::move(onSelect));
    title.clear();
    anchor = row;
    hasAnchor = anchored = true;
  }

  void showContext(const char* bookTitle, const freeink::ui::Rect& source, const StrId* optionIds,
                   bool pinned, std::function<void(int)> onSelect,
                   std::function<void(freeink::ui::Rect)> background = {}) {
    if (tenorchrome::kTouchShell) showAnchored(source, optionIds, 2, std::move(onSelect));
    else {
      const char* labels[] = {I18N.get(optionIds[0]), I18N.get(optionIds[1])};
      show(bookTitle, labels, 2, -1, std::move(onSelect));
    }
    contextMenu = true;
    contextPinned = pinned;
    contextBackground = std::move(background);
  }

  // One option down (1) or up (-1), with wrap: what the up and down buttons do, for a row tilt.
  void step(const int direction) {
    const int count = static_cast<int>(ownedStrings.size());
    if (!active || count == 0) return;
    selectedIndex = (selectedIndex + (direction > 0 ? 1 : -1) + count) % count;
  }
  int selected() const { return selectedIndex; }
  uint32_t selectionSeed() const { return mark_.get(selectedIndex); }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const int count = static_cast<int>(ownedStrings.size());
    if (count == 0) { active = false; return true; }
    const freeink::ui::InputSnapshot snap = touchSnapshotFrom(input);
    int swipeDy = 0;
    unsigned long swipeMs = 0;
    if (tenorchrome::kTouchShell && shownRows < count && input.wasVerticalSwipe(swipeDy, swipeMs)) {
      scrollTop = std::max(0, std::min(count - shownRows, scrollTop + touchscroll::rows(swipeDy, swipeMs, shownPitch,
                                                                                         std::max(1, shownRows - 1))));
      requestUpdate();
      return true;
    }
    if (snap.touchPressed || snap.touchReleased || snap.touchHeld) {
      // Interactions are registered on the render task; only route once the
      // first render after show() has populated the table (uiReady handshake).
      if (uiReady) {
        const freeink::ui::ActionEvent event = interactions.routePublished(snap);
        if (event && event.action == ACTION_OPTION) {
          // Tap released on an option: select it, fire, dismiss.
          selectedIndex = event.value;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          requestUpdate();
          return true;
        }
        if (event && event.action == ACTION_PAGE) {
          selectedIndex = std::max(0, std::min(count - 1, static_cast<int>(event.value)));
          requestUpdate();
          return true;
        }
        if (event && event.action == ACTION_CHROME) {
          // Taps on the dialog chrome (title, padding) keep the popup open.
          return true;
        }
        if (snap.touchReleased && snap.touchX >= 0) {
          // Tap released outside the dialog: dismiss without firing. Swipe-end
          // releases arrive with -1,-1 coords and fall through (no dismiss).
          active = false;
          requestUpdate();
          return true;
        }
        if (snap.touchPressed && !tenorchrome::kTouchShell) {
          // Touch-down on an option moves the highlight (route() latched the
          // hit as the active interaction; read it back, no re-hit-testing).
          const int16_t idx = interactions.activeIndex();
          if (idx >= 0) {
            const freeink::ui::Interaction& hit = interactions.publishedData()[idx];
            if (hit.action == ACTION_OPTION && selectedIndex != hit.value) {
              selectedIndex = hit.value;
              requestUpdate();
            }
          }
        }
      }
      return true;
    }

    if (input.wasPressed(MappedInputManager::Button::NavPrevious)) {
      selectedIndex = (selectedIndex - 1 + count) % count;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::NavNext)) {
      selectedIndex = (selectedIndex + 1) % count;
      requestUpdate();
      return true;
    } else if (input.wasReleased(MappedInputManager::Button::Confirm)) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (input.wasReleased(MappedInputManager::Button::Back)) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    PageScrollbar::instance().beginPaint();
    namespace fui = freeink::ui;

    // Per-render target: a GfxRendererTarget is a renderer reference plus
    // three font ids, so rebuilding it here is trivially cheap and always
    // tracks the live orientation and uiScale fonts; a target held across
    // show() would stale-bind both after a rotation or scale change.
    fui::GfxRendererTarget target = makeUiTarget(renderer);
    const fui::ThemeTokens& theme = refreshSharedUiThemeTokens(target);
    // Frame stores a const DeviceContext&; keep it in a local that outlives
    // the frame (a deviceContext() temporary would dangle).
    const fui::DeviceContext device = target.deviceContext();
    // Routing happens on the loop task against the member buffer; the frame
    // itself never dispatches, so it gets an empty snapshot.
    const fui::InputSnapshot noInput{};

    // Builds into the generation handleInput()'s routePublished()/
    // publishedData() aren't currently reading, so the loop task never sees
    // this table mid-rebuild - see publish() below and
    // InteractionBuffer::beginPublishCycle().
    interactions.beginPublishCycle();
    fui::Frame<INTERACTION_CAPACITY> frame(target, device, noInput, interactions);
    if (tenorchrome::kTouchShell) {
      renderAnchored(renderer, frame, device.screen());
      PageScrollbar::instance().endPaint(&renderer);
      interactions.publish();
      uiReady = true;
      return;
    }

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int totalOptions = static_cast<int>(ownedStrings.size());
    const uint8_t count = static_cast<uint8_t>(totalOptions > MAX_OPTIONS ? MAX_OPTIONS : totalOptions);

    fui::DialogOption options[MAX_OPTIONS];
    for (uint8_t i = 0; i < count; ++i) {
      options[i].label = ownedStrings[i].c_str();
      options[i].action = ACTION_OPTION;
      options[i].value = static_cast<int16_t>(i);
      options[i].state = (i == selectedIndex) ? fui::StateFocused : fui::StateNormal;
    }

    fui::OptionDialogProps props;
    props.title = title.c_str();
    props.headline = headline.empty() ? nullptr : headline.c_str();
    props.options = options;
    props.optionCount = count;
    props.verticalOptions = true;
    // Touch only: physical buttons stay on the legacy wrap/confirm path above,
    // so the buffer never competes with it for focus/confirm dispatch.
    props.inputMask = fui::InputTouch;
    props.titleText.font = fui::GfxRendererTarget::FONT_BODY;
    props.titleText.bold = true;
    props.titleText.align = fui::TextAlign::Center;
    // Captions like "Remove from Recent Books?" overflow the narrow portrait
    // dialog in one line; let them wrap and the panel grow.
    const bool contextCross = contextMenu && !shell::isUgly();
    const std::string fittedTitle = contextCross
        ? renderer.truncatedText(uiScaleSpec().bodyFontId, title.c_str(), 300, EpdFontFamily::BOLD) : title;
    if (contextCross) props.title = fittedTitle.c_str();
    props.titleText.maxLines = contextCross ? 1 : 2;
    props.headlineText.font = fui::GfxRendererTarget::FONT_BODY;
    props.headlineText.align = fui::TextAlign::Center;
    props.headlineText.maxLines = 3;
    props.buttonText.font = fui::GfxRendererTarget::FONT_BODY;
    const int16_t innerPadding = static_cast<int16_t>(metrics.optionPopupInnerPadding);
    props.padding = fui::Insets{innerPadding, innerPadding, innerPadding, innerPadding};
    props.gap = static_cast<int16_t>(metrics.optionPopupItemSpacing);
    // Rounded invert-fill themes use a black pill, not the default gray focus cursor; button devices
    // take the same ringed white pill as their lists.
    if (theme.listSelectionStyle == fui::SelectionStyle::InvertFill && theme.listRowRadius > 0) {
      props.buttonStyles = fui::defaultButtonStyles();
      if (BoardConfig::hasTouch()) {
        props.buttonStyles.focused = props.buttonStyles.selected;
      } else {
        tenorPillSelection(props.buttonStyles);
      }
      // Its options keep the half-circle ends on button boards, where list rows cap the radius at half a one-line row.
      fui::setStyleRadius(props.buttonStyles, BoardConfig::hasTouch() ? theme.listRowRadius : TENOR_PILL_RADIUS);
    }
    // defaultPopupStyles() has no border, so opt in using the per-theme frame metrics.
    props.styles = fui::defaultPopupStyles();
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
    props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
    if (contextCross) props.styles.normal.radius = 28;
    props.styles.selected = props.styles.normal;
    props.styles.focused = props.styles.normal;
    props.styles.active = props.styles.normal;
    props.styles.disabled = props.styles.normal;
    props.buttonHeight =
        fui::clampI16(target.lineHeight(fui::GfxRendererTarget::FONT_BODY) + metrics.optionPopupSelectionVPadding * 2);

    const fui::Rect screen = device.screen();
    int textWidth = renderer.getTextWidth(uiScaleSpec().bodyFontId, title.c_str(), EpdFontFamily::BOLD);
    for (const auto& label : ownedStrings)
      textWidth = std::max(textWidth, renderer.getTextWidth(uiScaleSpec().bodyFontId, label.c_str()));
    if (contextCross) textWidth = renderer.getTextWidth(uiScaleSpec().bodyFontId, fittedTitle.c_str(), EpdFontFamily::BOLD);
    if (contextCross) for (const auto& label : ownedStrings)
      textWidth = std::max(textWidth, renderer.getTextWidth(uiScaleSpec().bodyFontId, label.c_str()));
    const int maxWidth = contextCross ? 396
        : std::min<int>(screen.width * 3 / 4, screen.width - metrics.optionPopupDialogSideMargin * 2);
    // A row that marks its value: the tick 16 px in from the row's edge, 12 px to the label, 16 px after it (drawChoice).
    const int rowWidth = !marked ? textWidth + 4 * innerPadding
                         : shell::isUgly() ? textWidth + 4 * innerPadding + 36
                                           : textWidth + 2 * innerPadding + 16 + 24 + 12 + 16;
    const auto sized = optionPopupLayout({0, 0, maxWidth, screen.height}, {}, rowWidth, 1, false);
    const int16_t width = fui::clampI16(sized.width);
    const int fullHeight = fui::optionDialogHeight(target, props, width);
    fui::Rect available = screen;
    if (contextCross) {
      const auto body = tenorchrome::x3ScreenLayout(renderer).bodyRect;
      available = {fui::clampI16(body.x), fui::clampI16(body.y), fui::clampI16(body.width), fui::clampI16(body.height)};
    }
    int firstOption = 0, visibleOptions = totalOptions;
    const bool small = normalizedUiTextSize(SETTINGS.uiTextSize) == 0;
    if (!small || totalOptions > MAX_OPTIONS || fullHeight > screen.height) {
      if (!contextCross) {
        available = device.safeRect();
        available.height = fui::clampI16(available.height - metrics.buttonHintsHeight);
      }
      props.optionCount = 0;
      const int headerHeight = fui::optionDialogHeight(target, props, width);
      const auto window = optionPopupWindow(totalOptions, selectedIndex, available.height, headerHeight,
                                            props.buttonHeight + props.gap);
      firstOption = window.first;
      visibleOptions = window.count;
      int slot = 0;
      if (window.paged) {
        options[slot++] = {tr(STR_PREV_PAGE), ACTION_PAGE,
                           static_cast<int16_t>(std::max(0, window.first - 1)), fui::StateNormal, window.first > 0};
      }
      for (int i = 0; i < window.count; ++i) {
        const int index = window.first + i;
        options[slot].label = ownedStrings[index].c_str();
        options[slot].action = ACTION_OPTION;
        options[slot].value = static_cast<int16_t>(index);
        options[slot].state = index == selectedIndex ? fui::StateFocused : fui::StateNormal;
        ++slot;
      }
      if (window.next) {
        options[slot++] = {tr(STR_NEXT_PAGE), ACTION_PAGE,
                           static_cast<int16_t>(std::min(totalOptions - 1, window.first + window.count)), fui::StateNormal,
                           window.first + window.count < totalOptions};
      }
      props.optionCount = static_cast<uint8_t>(slot);
    }
    const int16_t height = fui::optionDialogHeight(target, props, width);
    const auto placed = optionPopupLayout({available.x, available.y, available.width, available.height}, {},
                                          width, height, false);
    const fui::Rect dialogRect{fui::clampI16(placed.x), fui::clampI16(placed.y),
                              fui::clampI16(placed.width), fui::clampI16(placed.height)};
    lastRenderedFrame = dialogRect;
#ifdef SIMULATOR
    if (contextCross) LOG_INF("MENU", "context box=%d,%d,%d,%d row=%d source=button", dialogRect.x,
                              dialogRect.y, dialogRect.width, dialogRect.height, props.buttonHeight);
    else LOG_INF("DIALOG", "popup box=%d,%d,%d,%d", dialogRect.x, dialogRect.y, dialogRect.width, dialogRect.height);
#endif
    if (contextCross && contextBackground) contextBackground(dialogRect);
    if (contextCross) renderer.fillRect(dialogRect.x - 8, dialogRect.y - 8,
                                        dialogRect.width + 16, dialogRect.height + 16, false);

    // Chrome guard first, options after: route() scans newest-first, so the
    // option buttons win inside the dialog and the guard absorbs the rest.
    frame.hit(dialogRect, ACTION_CHROME, 0, fui::InputTouch);
    if (shell::isUgly()) renderUgly(renderer, frame, dialogRect, props);
    else {
      CenteredText context{renderer, target, marked, marked ? ownedStrings[selectedIndex].c_str() : nullptr, &ownedStrings};
      target.setTextSink(centerOptionText, &context);
      fui::optionDialog(frame, dialogRect, props);
      target.setTextSink(nullptr, nullptr);
    }
    PageScrollbar::instance().draw(renderer, target, dialogRect.y, dialogRect.bottom(),
                                   totalOptions * dialogRect.height / std::max(1, visibleOptions),
                                   firstOption * dialogRect.height / std::max(1, visibleOptions), this, dialogRect.right());
    PageScrollbar::instance().endPaint(&renderer);
    // Atomically make this generation the one handleInput() reads, now that
    // every hit() call for this frame is done.
    interactions.publish();
    uiReady = true;
  }

  bool isActive() const { return active; }

  bool vuaMan(const GfxRenderer& renderer, const std::vector<std::string>& options) const {
    namespace fui = freeink::ui;
    const int total = static_cast<int>(options.size());
    if (total <= 0 || total > MAX_OPTIONS) return false;
    fui::GfxRendererTarget target = makeUiTarget(renderer);
    const fui::DeviceContext device = target.deviceContext();
    const auto& metrics = UITheme::getInstance().getMetrics();
    fui::DialogOption opts[MAX_OPTIONS];
    for (int i = 0; i < total; ++i) {
      opts[i].label = options[i].c_str();
      opts[i].action = ACTION_OPTION;
      opts[i].value = static_cast<int16_t>(i);
    }
    fui::OptionDialogProps props;
    props.title = options[0].c_str();  // mot dong tieu de, noi dung khong doi chieu cao
    props.options = opts;
    props.optionCount = static_cast<uint8_t>(total);
    props.verticalOptions = true;
    props.titleText.font = fui::GfxRendererTarget::FONT_BODY;
    props.titleText.bold = true;
    props.buttonText.font = fui::GfxRendererTarget::FONT_BODY;
    const int16_t innerPadding = static_cast<int16_t>(metrics.optionPopupInnerPadding);
    props.padding = fui::Insets{innerPadding, innerPadding, innerPadding, innerPadding};
    props.gap = static_cast<int16_t>(metrics.optionPopupItemSpacing);
    props.buttonHeight =
        fui::clampI16(target.lineHeight(fui::GfxRendererTarget::FONT_BODY) + metrics.optionPopupSelectionVPadding * 2);
    const fui::Rect screen = device.screen();
    const int16_t width =
        fui::clampI16(std::min<int>(screen.width * 3 / 4, screen.width - metrics.optionPopupDialogSideMargin * 2));
    return fui::optionDialogHeight(target, props, width) <= screen.height;
  }

  // Close without firing the callback (the surface under the popup is going
  // away, e.g. its host screen closes from outside the popup's own input).
  void dismiss() {
    active = false;
    onSelectCallback = nullptr;
  }

 private:
  static void drawAction(const GfxRenderer& renderer, int font, int x, int ry, int w, int ROW,
                         const char* caption, int icon) {
    const int top = renderer.getTextInkTop(font, caption, EpdFontFamily::REGULAR);
    const int bottom = renderer.getTextInkBottom(font, caption, EpdFontFamily::REGULAR);
#ifdef SIMULATOR
    LOG_INF("MENU", "context label=%s complete=1 width=%d room=%d", caption,
            renderer.getTextWidth(font, caption, EpdFontFamily::REGULAR), w - 68);
#endif
    renderer.drawText(font, x + 16, ry + (ROW - top - bottom) / 2, caption, true,
                      EpdFontFamily::REGULAR);
    tenorchrome::drawContextActionIcon(renderer, icon, x + w - 40, ry + (ROW - 24) / 2);
  }

  static void drawChoice(const GfxRenderer& renderer, int font, int x, int ry, int w, int ROW,
                         const char* caption, bool marked, bool chosen) {
    constexpr int TEXT_X = 16, MARK = 24, GAP = 12;
    const int room = w - 2 * TEXT_X - (marked ? MARK + GAP : 0);
    const auto label = renderer.truncatedText(font, caption, room, EpdFontFamily::REGULAR);
    const int inkTop = renderer.getTextInkTop(font, label.c_str(), EpdFontFamily::REGULAR);
    const int inkBottom = renderer.getTextInkBottom(font, label.c_str(), EpdFontFamily::REGULAR);
    const int textY = ry + (ROW - inkTop - inkBottom) / 2;
    renderer.drawText(font, x + TEXT_X + (marked ? MARK + GAP : 0), textY, label.c_str(), true,
                      EpdFontFamily::REGULAR);
    if (chosen) {
      // The tick's ink is 13 rows (5 to 17 of its 24): its box is centred on the row, not on the label's ink, which an
      // odd row height and an accent above or a hook below the label moved by a row.
      const int markY = ry + (ROW - (MARK - 1)) / 2;
      tenorchrome::drawBarIcon(renderer, icon_row_chosen_24.bits, MARK, MARK, x + TEXT_X, markY, true);
    }
  }

  struct CenteredText {
    const GfxRenderer& renderer;
    freeink::ui::GfxRendererTarget& target;
    bool marked;
    const char* selected;
    const std::vector<std::string>* values;
  };

  static void centerOptionText(void* context, freeink::ui::Rect rect, const char* label,
                               const freeink::ui::TextStyle& style) {
    namespace fui = freeink::ui;
    auto& ctx = *static_cast<CenteredText*>(context);
    const int font = uiScaleSpec().bodyFontId;
    if (ctx.marked && !style.bold &&
        std::any_of(ctx.values->begin(), ctx.values->end(), [&](const auto& value) { return value == label; })) {
      // The row is the whole button: the SDK hands the text the button less its padding.
      const fui::Insets pad = fui::ButtonProps{}.padding;
      drawChoice(ctx.renderer, font, rect.x - pad.left, rect.y - pad.top, rect.width + pad.left + pad.right,
                 rect.height + pad.top + pad.bottom, label, true, ctx.selected && std::strcmp(ctx.selected, label) == 0);
      return;
    }
    if (style.align != fui::TextAlign::Center || rect.height < ctx.renderer.getLineHeight(font) ||
        ctx.renderer.getTextWidth(font, label) > rect.width) {
      ctx.target.setTextSink(nullptr, nullptr);
      ctx.target.text(rect, label, style);
      ctx.target.setTextSink(centerOptionText, context);
      return;
    }
    const auto family = ctx.renderer.getFontMap().find(font);
    int left = 4096, right = -4096;
    const auto* p = reinterpret_cast<const unsigned char*>(label);
    const auto* start = p;
    while (*p) {
      const auto* before = p;
      const uint32_t cp = utf8NextCodepoint(&p);
      const auto* glyph = family == ctx.renderer.getFontMap().end() ? nullptr : family->second.getGlyph(cp);
      if (!glyph || !glyph->width || !glyph->height) continue;
      const std::string prefix(reinterpret_cast<const char*>(start), before - start);
      const int pen = ctx.renderer.getTextAdvanceX(font, prefix.c_str(), EpdFontFamily::REGULAR);
      const auto* data = family->second.getData(EpdFontFamily::REGULAR);
      const auto* bitmap = data ? ctx.renderer.getGlyphBitmap(data, glyph) : nullptr;
      int inkLeft = glyph->width, inkRight = -1;
      for (int row = 0; bitmap && row < glyph->height; ++row)
        for (int col = 0; col < glyph->width; ++col) {
          const int pixel = row * glyph->width + col;
          const bool ink = data->is2Bit ? ((bitmap[pixel / 4] >> (6 - 2 * (pixel % 4))) & 3) != 0
                                        : ((bitmap[pixel / 8] >> (7 - pixel % 8)) & 1) != 0;
          if (ink) { inkLeft = std::min(inkLeft, col); inkRight = std::max(inkRight, col); }
        }
      if (!bitmap) { inkLeft = 0; inkRight = glyph->width - 1; }
      if (inkRight >= inkLeft) {
        left = std::min(left, pen + glyph->left + inkLeft);
        right = std::max(right, pen + glyph->left + inkRight + 1);
      }
    }
    if (right <= left) {
      ctx.target.setTextSink(nullptr, nullptr);
      ctx.target.text(rect, label, style);
      ctx.target.setTextSink(centerOptionText, context);
      return;
    }
    const int top = ctx.renderer.getTextInkTop(font, label, EpdFontFamily::REGULAR);
    const int bottom = ctx.renderer.getTextInkBottom(font, label, EpdFontFamily::REGULAR);
    // The production font rounds long glyph runs one pixel left of the
    // continuous ink center. Nudge those runs after measuring their bitmap.
    const int x = rect.x + (rect.width - (right - left)) / 2 - left + ((right - left) > 150 ? 1 : 0);
    const int y = rect.y + (rect.height - (bottom - top)) / 2 - top;
    ctx.renderer.drawText(font, x, y, label, !style.inverted);
  }

  mutable ugly::MarkState mark_;
  // A bounded page keeps the stack and published hit table small. All source
  // options remain reachable through physical selection and touch page rows.
  static constexpr int MAX_OPTIONS = 16;
  static constexpr size_t INTERACTION_CAPACITY = MAX_OPTIONS + 1;
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  static constexpr freeink::ui::ActionId ACTION_CHROME = 2;
  static constexpr freeink::ui::ActionId ACTION_PAGE = 3;

  // Returns where the ink went, for the circle on the focused option.
  static freeink::ui::Rect uglyText(const GfxRenderer& renderer, const freeink::ui::Rect& rect, const char* label) {
    if (!label || rect.empty()) return {};
    const auto clip = renderer.getClipRect();
    const int left = std::max<int>(rect.x, clip[0]), top = std::max<int>(rect.y, clip[1]);
    renderer.setClipRect(left, top, std::max(0, std::min<int>(rect.right(), clip[0] + clip[2]) - left),
                        std::max(0, std::min<int>(rect.bottom(), clip[1] + clip[3]) - top));
    const auto fitted = ugly::fit(renderer, ugly::Size::S22, label, rect.width - 8);
    const auto ink = ugly::textBounds(renderer, ugly::Size::S22, fitted.c_str());
    const int x = rect.x + (rect.width - (ink.x1 - ink.x0)) / 2 - ink.x0;
    const int base = rect.y + (rect.height - (ink.y1 - ink.y0)) / 2 - ink.y0;
    ugly::text(renderer, ugly::Size::S22, x, base, fitted.c_str());
    renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
    return {static_cast<int16_t>(x + ink.x0), static_cast<int16_t>(base + ink.y0),
            static_cast<int16_t>(ink.x1 - ink.x0), static_cast<int16_t>(ink.y1 - ink.y0)};
  }

  static void uglyPaper(const GfxRenderer& renderer, const freeink::ui::Rect& rect) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    ugly::line(renderer, rect.x + 2, rect.y + 2, rect.right() - 3, rect.y + 1, 920, 2);
    ugly::line(renderer, rect.right() - 3, rect.y + 1, rect.right() - 2, rect.bottom() - 3, 921, 2);
    ugly::line(renderer, rect.right() - 2, rect.bottom() - 3, rect.x + 2, rect.bottom() - 2, 922, 2);
    ugly::line(renderer, rect.x + 2, rect.bottom() - 2, rect.x + 2, rect.y + 2, 923, 2);
  }

  template <typename Frame>
  void renderUgly(const GfxRenderer& renderer, Frame& frame, const freeink::ui::Rect& rect,
                  const freeink::ui::OptionDialogProps& props) const {
    namespace fui = freeink::ui;
    if (props.dimBackground) frame.target().fill(frame.screen(), fui::Paint::dither(fui::Color::LightGray));
    uglyPaper(renderer, rect);
    const auto content = rect.inset(props.padding);
    int16_t cursor = content.y;
    // Keep the SDK's wrapped header rows and reserved heights at each UI tier.
    for (const auto& header : {std::make_pair(props.title, props.titleText),
                              std::make_pair(props.headline, props.headlineText)}) {
      if (!header.first) continue;
      const int16_t lh = frame.target().lineHeight(header.second.font);
      fui::layoutText(frame.target(), fui::Rect{content.x, cursor, content.width, 1}, header.first, header.second,
                      [&](const char* line, fui::Rect) {
                        uglyText(renderer, {content.x, cursor, content.width, lh}, line);
                        cursor = static_cast<int16_t>(cursor + lh);
                      });
      cursor = static_cast<int16_t>(cursor + props.gap);
    }
    const int buttonsH = props.optionCount * props.buttonHeight + std::max(0, props.optionCount - 1) * props.gap;
    for (int i = 0; i < props.optionCount; ++i) {
      const auto& option = props.options[i];
      const fui::Rect row{content.x, static_cast<int16_t>(content.bottom() - buttonsH + i * (props.buttonHeight + props.gap)),
                          content.width, props.buttonHeight};
      if (option.enabled && option.action != fui::NO_ACTION)
        frame.hit(fui::ensureMinTouchRect(row, frame.device().minTouchSize, frame.screen()),
                  option.action, option.value, props.inputMask, option.state);
      const auto ink = uglyText(renderer, row, option.label);
      if (fui::hasState(option.state, fui::StateFocused)) uglychrome::ring(renderer, ink, selectionSeed());
    }
  }

  // Held-row actions and value menus share measured sizing and row anchoring.
  // A question (nothing tapped under it: a confirmation) stands over the bar at the foot, its caption and text
  // inside the frame. Under the bar at most: more rows than fit scroll by a swipe, with the scroll bar.
  template <typename Frame>
  void renderAnchored(const GfxRenderer& renderer, Frame& frame, const freeink::ui::Rect& screen) const {
    namespace fui = freeink::ui;
    constexpr int font = UI_12_FONT_ID, PAD = 6, RADIUS = 20, MARK = 24;
    const bool narrow = anchored;
    const bool actions = contextMenu && !shell::isUgly();
    const int ROW = actions ? 60 : inFrame || narrow ? 56 : std::max(60, renderer.getLineHeight(font) + 16);
    const int pad = inFrame ? std::min(PAD, std::max(0, (frameRect.height - ROW) / 2)) : PAD;
    const int TEXT_X = narrow ? 24 : 16;
    const int count = static_cast<int>(ownedStrings.size());
    int textWidth = 0;
    for (const auto& label : ownedStrings)
      textWidth = std::max(textWidth, renderer.getTextWidth(font, label.c_str(), EpdFontFamily::REGULAR));
    const int maxWidth = screen.width - 2 * tenorchrome::FOOT_BACK_X;
    // The caption and the subject of a question, wrapped inside the frame.
    fui::TextStyle head;
    head.font = fui::GfxRendererTarget::FONT_BODY;
    head.maxLines = 6;
    const char* heads[2] = {title.empty() || hasAnchor ? nullptr : title.c_str(),
                            headline.empty() ? nullptr : headline.c_str()};
    // The frame is as wide as its caption and subject ask too, up to the screen's room; past that they wrap by word.
    int headWidth = 0;
    for (const char* text : heads) {
      if (!text) continue;
      head.bold = text == heads[0];
      headWidth = std::max<int>(headWidth, frame.target().measureText(head.font, text, head).width);
    }
    const int w = inFrame ? frameRect.width : std::min(maxWidth,
        std::max(textWidth + (actions ? 68 : 2 * TEXT_X + (marked && !narrow ? MARK + 12 : 0)),
                 headWidth ? headWidth + 2 * TEXT_X : 0));
    const int captionX = inFrame ? frameRect.x : tenorchrome::FOOT_BACK_X;
    const int lh = frame.target().lineHeight(head.font);
    const fui::Rect headRect{static_cast<int16_t>(captionX + TEXT_X), 0, static_cast<int16_t>(w - 2 * TEXT_X), 1};
    // Wrapped once a show (a long warning costs more than the rest of the popup), kept for its repaints.
    if (!headLaid) {
      headLaid = true;
      headLines.clear();
      boldLines = 0;
      for (const char* text : heads) {
        if (!text) continue;
        head.bold = text == heads[0];
        fui::layoutText(frame.target(), headRect, text, head, [&](const char* line, fui::Rect) {
          headLines.emplace_back(line);
        });
        if (head.bold) boldLines = static_cast<int>(headLines.size());
      }
    }
    int headH = static_cast<int>(headLines.size()) * lh;
    if (headH) headH += 2 * PAD + 8;
    // A single-row group keeps its own frame; the choices take the room before the caption.
    if (inFrame && headH + ROW + 2 * pad > frameRect.height) headH = 0;
    const int top = inFrame ? frameRect.y : tenorchrome::contentTop();
    const int bottom = inFrame ? frameRect.bottom() : tenorchrome::footBackTop(screen.height) - 8;
    const int rows = inFrame ? rowsInFrame(frameRect.height, headH, ROW, count)
                             : std::max(1, std::min({count, MAX_OPTIONS, std::max(1, (bottom - top - 2 * PAD - headH) / ROW)}));
    if (scrollTop < 0) scrollTop = selectedIndex - rows / 2;
    scrollTop = std::max(0, std::min(scrollTop, count - rows));
    const int first = scrollTop;
    shownRows = rows;
    shownPitch = ROW;
    const int groupHeight = actions && rows > 1 ? 8 : 0;
    const int h = headH + rows * ROW + 2 * pad + groupHeight;
    const int menuTop = actions ? top - 8 : top;
    const auto placed = optionPopupLayout({tenorchrome::FOOT_BACK_X, menuTop, maxWidth, bottom - menuTop},
        hasAnchor ? OptionPopupBox{anchor.x, anchor.y, anchor.width, anchor.height} : OptionPopupBox{},
        w, h, true, !narrow, actions);
    const fui::Rect box = inFrame ? frameRect : fui::Rect{fui::clampI16(placed.x), fui::clampI16(placed.y),
                                                        fui::clampI16(placed.width), fui::clampI16(placed.height)};
    const int x = box.x, y = box.y;
    lastRenderedFrame = box;
#ifdef SIMULATOR
    if (actions) LOG_INF("MENU", "context box=%d,%d,%d,%d row=%d source=%d,%d,%d,%d", x, y,
                         box.width, box.height, ROW, anchor.x, anchor.y, anchor.width, anchor.height);
#endif
    if (actions && contextBackground) contextBackground(box);
    if (actions) renderer.fillRect(x - 8, y - 8, box.width + 16, box.height + 16, false);
    if (shell::isUgly()) uglyPaper(renderer, box);
    else {
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, RADIUS, Color::White);
      tenorchrome::drawRoundRing(renderer, box.x, box.y, box.width, box.height, tenorchrome::PANEL_RADIUS, 2, true);
    }
    frame.hit(box, ACTION_CHROME, 0, fui::InputTouch);
    int cursor = y + pad + (headH ? PAD + 4 : 0);
    for (size_t i = 0; headH && i < headLines.size(); ++i) {
      head.bold = static_cast<int>(i) < boldLines;
      frame.target().text(fui::Rect{static_cast<int16_t>(x + TEXT_X), static_cast<int16_t>(cursor), headRect.width, static_cast<int16_t>(lh)},
                          headLines[i].c_str(), head);
      cursor += lh;
    }
    const int rowsTop = y + pad + headH;
    if (headH) tenorchrome::drawRowRule(renderer, rowsTop - 1, x + TEXT_X, x + w - TEXT_X);
    for (int r = 0; r < rows; ++r) {
      const int i = first + r;
      const int ry = rowsTop + r * ROW + (r > 0 ? groupHeight : 0);
      const bool chosen = marked && !narrow && i == selectedIndex;
      if (shell::isUgly()) uglyText(renderer, {static_cast<int16_t>(x + TEXT_X), static_cast<int16_t>(ry),
                                         static_cast<int16_t>(w - 2 * TEXT_X), static_cast<int16_t>(ROW)}, ownedStrings[i].c_str());
      else if (actions) {
        drawAction(renderer, font, x, ry, box.width, ROW, ownedStrings[i].c_str(),
                   i == 1 ? 2 : contextPinned ? 1 : 0);
      } else {
        drawChoice(renderer, font, x, ry, w, ROW, ownedStrings[i].c_str(), marked && !narrow, chosen);
      }
      if (r + 1 < rows) {
        if (actions) for (int dy = 2; dy < 6; ++dy)
          tenorchrome::drawRowRule(renderer, ry + ROW + dy, x + 2, x + box.width - 2);
        else tenorchrome::drawRowRule(renderer, ry + ROW - 1, x + TEXT_X, x + w - TEXT_X);
      }
      frame.hit(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(ry), box.width, static_cast<int16_t>(ROW)},
                ACTION_OPTION, static_cast<int16_t>(i), fui::InputTouch);
    }
    PageScrollbar::instance().draw(renderer, frame.target(), rowsTop, rowsTop + rows * ROW,
                                   count * ROW, first * ROW, this);
  }


  // Every option stays selectable, not only the first MAX_OPTIONS: longer lists page (see render).
  // An empty list is dismissed by the first handleInput().
  // currentIndex < 0: a question or a list of actions, no value in use to mark (the buttons start on the first).
  void activate(int currentIndex, std::function<void(int)> onSelect) {
    mark_.reset();
    marked = currentIndex >= 0;
    selectedIndex = std::max(0, std::min(currentIndex, static_cast<int>(ownedStrings.size()) - 1));
    // Touch: the row whose tap opens this list, when a tap opens it.
    anchor = tenorchrome::kTouchShell ? UiAppHost::dispatchingRect() : freeink::ui::Rect{};
    hasAnchor = anchor.height > 0;
    scrollTop = -1;
    onSelectCallback = std::move(onSelect);
    uiReady = false;
    active = true;
    anchored = false;
    contextMenu = false;
    contextBackground = nullptr;
    inFrame = false;
    alignSelected = false;
    headLaid = false;
  }

  void activateInFrame(const freeink::ui::Rect& parentFrame) {
    frameRect = parentFrame;
    if (tenorchrome::kTouchShell && marked) {
      alignValueTo(hasAnchor ? anchor : parentFrame);
      inFrame = false;
      headLaid = false;
      scrollTop = -1;
      return;
    }
    inFrame = !parentFrame.empty();
    anchored = false;
    hasAnchor = false;
    headLaid = false;
    scrollTop = -1;
  }

  bool active = false;
  bool contextMenu = false;
  bool contextPinned = false;
  std::function<void(freeink::ui::Rect)> contextBackground;
  bool anchored = false;
  bool inFrame = false;
  bool hasAnchor = false;
  bool alignSelected = false;
  bool marked = true;
  freeink::ui::Rect anchor{};
  freeink::ui::Rect frameRect{};
  mutable freeink::ui::Rect lastRenderedFrame{};
  // Touch: the first row shown (-1: around the value in use) and what the last frame showed, for a swipe.
  mutable int scrollTop = -1;
  mutable int shownRows = MAX_OPTIONS;
  mutable int shownPitch = 56;
  // Touch: the caption and subject lines as the first frame wrapped them (boldLines: the caption's).
  mutable bool headLaid = false;
  mutable std::vector<std::string> headLines;
  mutable int boldLines = 0;
  std::string title;
  std::string headline;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
  // Written by the render task (frame registration), routed by the loop task;
  // uiReady closes the rebuild window exactly like UiListActivity::uiReady.
  mutable freeink::ui::InteractionBuffer<INTERACTION_CAPACITY> interactions;
  mutable std::atomic<bool> uiReady{false};
};
