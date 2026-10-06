#pragma once
#include <I18n.h>

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
#include "components/TenorMenuChrome.h"
#include "components/UiAppHelpers.h"
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
  void show(const char* titleStr, const char* headlineStr, const char* const* options, int optionCount,
            int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    headline = headlineStr ? headlineStr : "";
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings = options;
    activate(currentIndex, std::move(onSelect));
  }

  // Touch shell: the actions of a held row in a menu anchored to the row, no title: under it when the
  // row is in the upper half and the menu fits over the bar at the foot, else over it; left on the list's
  // margin. A tap outside closes it.
  void showAnchored(const freeink::ui::Rect& row, const StrId* optionIds, int optionCount,
                    std::function<void(int)> onSelect) {
    show(StrId::STR_NONE_OPT, optionIds, optionCount, 0, std::move(onSelect));
    title.clear();
    anchor = row;
    anchored = true;
  }

  // One option down (1) or up (-1), with wrap: what the up and down buttons do, for a row tilt.
  void step(const int direction) {
    const int count = static_cast<int>(ownedStrings.size());
    if (!active || count == 0) return;
    selectedIndex = (selectedIndex + (direction > 0 ? 1 : -1) + count) % count;
  }
  int selected() const { return selectedIndex; }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const int count = static_cast<int>(ownedStrings.size());
    if (count == 0) { active = false; return true; }
    const freeink::ui::InputSnapshot snap = touchSnapshotFrom(input);
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
        if (snap.touchPressed && !anchored) {
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
    if (anchored) {
      renderAnchored(renderer, frame, device.screen());
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
    props.titleText.maxLines = 2;
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
      fui::setStyleRadius(props.buttonStyles, theme.listRowRadius);
    }
    // defaultPopupStyles() has no border, so opt in using the per-theme frame metrics.
    props.styles = fui::defaultPopupStyles();
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
    props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
    props.styles.selected = props.styles.normal;
    props.styles.focused = props.styles.normal;
    props.styles.active = props.styles.normal;
    props.styles.disabled = props.styles.normal;
    props.buttonHeight =
        fui::clampI16(target.lineHeight(fui::GfxRendererTarget::FONT_BODY) + metrics.optionPopupSelectionVPadding * 2);

    // Fixed fraction of the screen, clamped by the theme's side margins; the
    // old max-text-width sizing is gone, long labels wrap inside the buttons.
    const fui::Rect screen = device.screen();
    const int16_t width =
        fui::clampI16(std::min<int>(screen.width * 3 / 4, screen.width - metrics.optionPopupDialogSideMargin * 2));
    const int fullHeight = fui::optionDialogHeight(target, props, width);
    fui::Rect available = screen;
    const bool small = normalizedUiTextSize(SETTINGS.uiTextSize) == 0;
    if (!small || totalOptions > MAX_OPTIONS || fullHeight > screen.height) {
      available = device.safeRect();
      available.height = fui::clampI16(available.height - metrics.buttonHintsHeight);
      props.optionCount = 0;
      const int headerHeight = fui::optionDialogHeight(target, props, width);
      const auto window = optionPopupWindow(totalOptions, selectedIndex, available.height, headerHeight,
                                            props.buttonHeight + props.gap);
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
    const fui::Rect dialogRect = fui::centeredRect(available, fui::Size{width, height});

    // Chrome guard first, options after: route() scans newest-first, so the
    // option buttons win inside the dialog and the guard absorbs the rest.
    frame.hit(dialogRect, ACTION_CHROME, 0, fui::InputTouch);
    if (shell::isUgly()) renderUgly(renderer, frame, dialogRect, props);
    else fui::optionDialog(frame, dialogRect, props);
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
    const int w = ugly::width(renderer, ugly::Size::S22, fitted.c_str()), asc = ugly::ascent(ugly::Size::S22);
    const int x = rect.x + (rect.width - w) / 2, base = rect.y + (rect.height + asc) / 2;
    ugly::text(renderer, ugly::Size::S22, x, base, fitted.c_str());
    renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
    return {static_cast<int16_t>(x), static_cast<int16_t>(base - asc), static_cast<int16_t>(w), static_cast<int16_t>(asc + 6)};
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
      if (fui::hasState(option.state, fui::StateFocused)) uglychrome::ring(renderer, ink);
    }
  }

  template <typename Frame>
  void renderAnchored(const GfxRenderer& renderer, Frame& frame, const freeink::ui::Rect& screen) const {
    namespace fui = freeink::ui;
    constexpr int font = UI_12_FONT_ID, ROW = 56, PAD = 6, TEXT_X = 24, RADIUS = 20, GAP = 4;
    const int count = std::min<int>(static_cast<int>(ownedStrings.size()), MAX_OPTIONS);
    int textWidth = 0;
    for (int i = 0; i < count; ++i) textWidth = std::max(textWidth, renderer.getTextWidth(font, ownedStrings[i].c_str()));
    const int x = tenorchrome::FOOT_BACK_X;
    const int w = std::min<int>(screen.width - 2 * x, std::max(180, textWidth + 2 * TEXT_X));
    const int h = count * ROW + 2 * PAD;
    const int top = tenorchrome::contentTop(), bottom = screen.height - tenorchrome::footBackReserve() + 8;
    const int below = anchor.y + anchor.height + GAP, above = anchor.y - GAP - h;
    const bool upperHalf = anchor.y + anchor.height / 2 < screen.height / 2;
    int y = upperHalf && below + h <= bottom ? below : above >= top ? above : below + h <= bottom ? below : bottom - h;
    y = std::max(top, y);
    if (shell::isUgly()) uglyPaper(renderer, {static_cast<int16_t>(x), static_cast<int16_t>(y),
                                        static_cast<int16_t>(w), static_cast<int16_t>(h)});
    else {
      renderer.fillRoundedRect(x, y, w, h, RADIUS, Color::White);
      tenorchrome::drawRoundRing(renderer, x, y, w, h, RADIUS, 2, true);
    }
    frame.hit(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)},
              ACTION_CHROME, 0, fui::InputTouch);
    for (int i = 0; i < count; ++i) {
      const int ry = y + PAD + i * ROW;
      if (shell::isUgly()) uglyText(renderer, {static_cast<int16_t>(x + TEXT_X), static_cast<int16_t>(ry),
                                         static_cast<int16_t>(w - 2 * TEXT_X), ROW}, ownedStrings[i].c_str());
      else renderer.drawText(font, x + TEXT_X, ry + (ROW - renderer.getLineHeight(font)) / 2, ownedStrings[i].c_str());
      if (i + 1 < count)
        for (int px = x + TEXT_X; px < x + w - TEXT_X; ++px)
          if (((px + ry + ROW - 1) & 1) == 0) renderer.drawPixel(px, ry + ROW - 1, true);
      frame.hit(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(ry), static_cast<int16_t>(w), static_cast<int16_t>(ROW)},
                ACTION_OPTION, static_cast<int16_t>(i), fui::InputTouch);
    }
  }

  // Every option stays selectable, not only the first MAX_OPTIONS: longer lists page (see render).
  // An empty list is dismissed by the first handleInput().
  void activate(int currentIndex, std::function<void(int)> onSelect) {
    selectedIndex = std::max(0, std::min(currentIndex, static_cast<int>(ownedStrings.size()) - 1));
    onSelectCallback = std::move(onSelect);
    uiReady = false;
    active = true;
    anchored = false;
  }

  bool active = false;
  bool anchored = false;
  freeink::ui::Rect anchor{};
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
