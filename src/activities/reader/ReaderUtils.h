#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include <cctype>
#include <string_view>

#include "MappedInputManager.h"
#include "ReaderTapZones.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline bool gestureAllowsSwipe(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::SWIPE_ONLY;
}

inline bool gestureAllowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY ||
         gesture == CrossPointSettings::INVERTED_TAP;
}

inline bool isRtlBookLanguage(std::string_view tag) {
  if (tag.size() < 2 || (tag.size() > 2 && tag[2] != '-' && tag[2] != '_')) return false;
  const auto first = std::tolower(static_cast<unsigned char>(tag[0]));
  const auto second = std::tolower(static_cast<unsigned char>(tag[1]));
  return (first == 'h' && second == 'e') || (first == 'i' && second == 'w') || (first == 'a' && second == 'r') ||
         (first == 'f' && second == 'a');
}

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
  bool prevLongPressed;
  bool nextLongPressed;
  bool prevButtonPressed;
  bool nextButtonPressed;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool longPressEnabled = SETTINGS.longPressButtonBehavior != SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prevButtonPressed =
      input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton);
  const bool nextButtonPressed =
      input.wasPressed(MappedInputManager::Button::PageForward) || input.wasPressed(nextButton);
  const bool prevLongPressed =
      longPressEnabled &&
      (input.wasLongPressed(MappedInputManager::Button::PageBack, SKIP_HOLD_MS) ||
       input.wasLongPressed(prevButton, SKIP_HOLD_MS));
  const bool nextLongPressed =
      longPressEnabled &&
      (input.wasLongPressed(MappedInputManager::Button::PageForward, SKIP_HOLD_MS) ||
       input.wasLongPressed(nextButton, SKIP_HOLD_MS));
  const bool prev = tiltPrev || prevButtonPressed;
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  // A Home-key board can map the Home key to the next page (HomeButtonAction::NextPage).
  const bool next =
      input.homeButtonAction() == HomeButtonAction::NextPage || tiltNext || nextButtonPressed || powerTurn;
  return {prev, next, tiltPrev || tiltNext, prevLongPressed, nextLongPressed, prevButtonPressed, nextButtonPressed};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

// The zone this frame's tap landed in (readertap::zoneAt decides), None without a tap. `bands` only for a
// reader that handles the touch shell's top and foot bands; the others keep the whole height for turning.
inline readertap::Zone tapZone(const GfxRenderer& renderer, const MappedInputManager& input, const bool rtlBook = false,
                               const bool bands = false) {
  if (!input.hasTouch()) return readertap::Zone::None;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return readertap::Zone::None;
  const bool controls = SETTINGS.touchReaderControls != 0;
  const readertap::Rules rules{
      controls && gestureAllowsTap(SETTINGS.pageTurnGesture),
      controls && gestureAllowsTap(SETTINGS.previousPageGesture),
      (SETTINGS.pageTurnGesture == CrossPointSettings::INVERTED_TAP ||
       SETTINGS.previousPageGesture == CrossPointSettings::INVERTED_TAP) != rtlBook,
      SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP,
      bands,
      SETTINGS.backTapZone};
  return readertap::zoneAt(x, y, renderer.getScreenWidth(), renderer.getScreenHeight(), rules);
}

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false, const bool bands = false) {
  TouchPageTurn result{false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  // A slow swipe never becomes a long-press chapter skip.
  const auto dir = input.wasSwipe();
  if (dir != MappedInputManager::SwipeDir::None) {
    result.next = dir == (rtlBook ? MappedInputManager::SwipeDir::Right : MappedInputManager::SwipeDir::Left) &&
                  gestureAllowsSwipe(SETTINGS.pageTurnGesture);
    result.prev = dir == (rtlBook ? MappedInputManager::SwipeDir::Left : MappedInputManager::SwipeDir::Right) &&
                  gestureAllowsSwipe(SETTINGS.previousPageGesture);
    return result;
  }

  const auto zone = tapZone(renderer, input, rtlBook, bands);
  result.next = zone == readertap::Zone::Next;
  result.prev = zone == readertap::Zone::Prev;
  if (result.next || result.prev) result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Tap in the center third of the screen: the tap path into the reader menu on every touch board
// (readertap::zoneAt keeps it out of the turn zones). The Off/Swipe Up alternatives are only surfaced on
// home-key boards (SettingsList), where the menu stays reachable through the key's long-press function.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  return tapZone(renderer, input) == readertap::Zone::Menu;
}

// Reader menu opens on the menu edge-swipe or a center-third tap. Home-key
// actions are configured separately from screen gestures.
// Menu gestures honor showReaderMenu independently of touchReaderControls,
// which only gates page-turn touch zones in detectTouchPageTurn().
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (input.wasMenuGesture()) return true;
  // Bottom-edge up-swipe variant: only selectable on home-key boards, where
  // Home is the capacitive key and the bottom edge is otherwise unused.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_SWIPE_UP && input.wasReaderMenuSwipeUp()) {
    return true;
  }
  return isTouchMenuTap(renderer, input);
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform - displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (renderer.grayscaleCapabilities().base != HalDisplay::GrayscaleBase::Combined) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return;
  }
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayGrayscaleBase(mode);
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered - status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined)
      renderer.cleanupGrayscaleWithFrameBuffer();
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

// A short Back returns to Recent/Continue. Holding Back keeps the global Home gesture.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager) {
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered = mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_BACK_OR_HOME_MS) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS;
  activityManager.goHome(longPress ? HomeMenuItem::NONE : HomeMenuItem::RECENT_CONTINUE);
  return true;
}

}  // namespace ReaderUtils
