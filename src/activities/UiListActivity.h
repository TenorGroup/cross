#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Base for activities hosting a single FreeInkUI list screen. UiAppHost owns
// the app-hosting protocol (render target, FreeInkApp, uiReady handshake);
// this base layers the list protocol on top: the touch-routing / swipe-scroll
// / button-navigation loop (swipes scroll the viewport without moving the
// selection; buttons move the selection and pull the viewport along via
// fui::ListNav), and the render skeleton (chrome, app, footer). Subclasses
// supply the data: item count, screen content, and what activating a row does.
//
// Screens that are not a single list (sliders, tab layouts, state machines)
// should NOT derive from this - they use UiAppHost directly.
class UiListActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  std::string navigationLabel() const override {
    const auto* title = headerTitle();
    return title ? title : "";
  }
  bool remembersNavigation() const override { return true; }
  void captureNavigation(MenuNavigationState& state) const override;
  void restoreNavigation(const MenuNavigationState& state) override;
  void loop() override;
  void render(RenderLock&&) override;

  // Kep con tro ve trong so dong dang co.
  //
  // ListNav::scrollBy() mang chu thich "clamp to range" nhung no chi kep `top`, con
  // `selected` di thang ra props. Man nao nap lai du lieu ma danh sach ngan di duoi chan
  // con tro dang nho thi chi so tro ra ngoai.
  //
  // Do 14/09/2026: hau qua chi la KHONG DONG NAO duoc to, vi ham list() chi DOI CHIEU
  // selectedIndex voi chi so dong chu khong lay no lam chi so mang, va duong bam co chot
  // chan rieng. Tuc loi tham my, khong phai doc ra ngoai bo nho.
  //
  // Dat o mot cho vi ca hai lop con deu can: ban co the va ban khong the. Public de bai
  // kiem goi thang duoc; no thuan nen khong giu trang thai gi.
  static int kepConTro(int chon, int soDong);

  void launchFavorite(const std::string& key, bool activate = true) {
    pendingFavorite = key;
    favoriteOrigin = key;
    activateFavorite = activate;
  }
  std::string navigationMemoryKey() const override {
    return favoriteOrigin.empty() ? Activity::navigationMemoryKey() : "favorite:" + favoriteOrigin;
  }

 protected:
  void renderUi();
  bool tabBandDrawn = false;
  void reserveFixedMenuContent(UiScreen& screen);
  // Base-owned row action; subclass-registered actions start at ACTION_USER.
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  static constexpr freeink::ui::ActionId ACTION_USER = 2;

  UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                 bool wantsTouchLongPress = false);

  // --- subclass contract -----------------------------------------------------
  // Current number of list rows (re-read every loop pass; may change).
  virtual int listCount() const = 0;
  // Build the screen: content margin, items, ListProps (call syncListViewport
  // right before screen.list). Runs on the render task via the base trampoline.
  virtual void buildScreen(UiScreen& screen) = 0;
  // Activate a row (touch tap or Confirm on the selection). Handlers that
  // leave this screen should call app.clearTapFlash() so a lingering flash
  // can't gray an unrelated element on the next render.
  virtual void activateIndex(int index) = 0;
  // Touch long-press on a row; only fires when the subclass opted in via the
  // wantsTouchLongPress constructor flag (rows must also carry InputLongPress).
  virtual void onRowLongPress(int index) {}
  // The selection/viewport state the loop, sync, and row dispatch operate on.
  // Default is the single `nav` member; UiTabListActivity redirects it to the
  // active tab's per-tab state.
  virtual freeink::ui::ListNav& activeNav() { return nav; }
  // Bounds-checked ACTION_ROW dispatch. Default: selection follows the tapped
  // row, then long-press/activate. UiTabListActivity remaps row -> ring.
  virtual void onRowAction(const freeink::ui::ActionEvent& event);
  // The button-navigation tail of loop(): front taps step one row, edge taps
  // page the viewport, and holds select the first/last visible row. UiTabListActivity replaces it with the ring walk.
  virtual void navigateButtons();

  // --- navigation intents ----------------------------------------------------
  // A press queues a move instead of performing it. The render task owns the
  // render lock for a whole frame, panel refresh included (390 ms measured on
  // the X3), and the debounce needs two samples more than 5 ms apart: a press
  // that arrives while loop() waits for that lock is never sampled, so it never
  // existed. loop() therefore never waits. It applies the queue with a
  // non-waiting lock the moment the panel is free, and holds Select/Back for
  // the pass that applies it so they land on the row the user can see.
  //
  // Only the main task reads and writes the queue.
  enum class NavIntent : uint8_t {
    StepNext,          // one row forward, wraps
    StepPrev,          // one row back, wraps
    PageNext,          // viewport down one page
    PagePrev,          // viewport up one page
    BoundaryFirst,     // first visible row (hold)
    BoundaryLast,      // last visible row (hold)
    BoundaryFirstRing, // same, moving the ring to a row (hold on tab screens)
    BoundaryLastRing,
    FirstRow,          // first row with the viewport pulled to it
    TabNext,           // step the tab one forward
    TabPrev,
  };
  static constexpr uint8_t NAV_QUEUE_SIZE = 8;
  void queueNavIntent(NavIntent intent);
  // Applies the queue; true once it is empty, false while the panel still owns
  // the render lock (the moves stay queued for the next pass).
  bool applyPendingNav();
  // One row step for StepNext/StepPrev. Default: the flat-list walk with wrap
  // and a follow. UiTabListActivity overrides it with the row ring (1..count).
  virtual void stepSelection(int direction);
  // Tab steps, dispatched with no render lock held: switching tabs rebuilds the
  // screen's data model and takes the lock itself.
  virtual void applyTabStep(int direction) {}
  // First row with the viewport pulled to it; ring screens address ring 1.
  virtual void applyFirstRow() {}
  // Subclass clamp, run under the render lock after every applied intent and on
  // every pass with an empty queue (UiTabListActivity: the ring cursor).
  virtual bool clampAfterNav() { return false; }
  // Release edges for Select/Back. A Select that arrives while moves are still
  // queued is remembered and reported on the pass that applies them, so it acts
  // on the row the user can see; with an empty queue it fires immediately. Back
  // never depends on the selection and is never held back. Subclasses that read
  // Confirm themselves must use confirmReleased() for that guarantee.
  bool confirmReleased();
  bool backReleased();
  // First hook in loop(); return true when the pass is consumed (popups, extra
  // buttons, gestures). Runs before the base button handling.
  virtual bool supportsFavorites() const { return false; }
  virtual std::string favoriteKey(int row) const { return {}; }
  virtual int favoriteSelectedRow() { return activeNav().selected; }
  virtual int focusFavorite(const std::string& key);
  virtual void favoritesChanged() {}
  virtual bool toggleFavorite(int row);
  virtual bool rowIsPinned(int row) const;
  void decoratePinnedRows(freeink::ui::ListProps& props);
  void reserveFavoriteHint(UiScreen& screen);
  virtual bool handleCustomInput() { return false; }
  // Back/Confirm handling; override wholesale for press/release or hold
  // variants. Return true when a button consumed the pass.
  virtual bool handleButtons();
  virtual void onBackButton() { finish(); }
  // Header band, drawn before the app renders. Default paints GUI.drawHeader
  // with headerTitle(); override either for custom chrome.
  virtual const char* headerTitle() const { return nullptr; }
  virtual void drawChrome();
  // Button hints, drawn after the app renders. Default: Back/Select/Up/Down.
  virtual void drawFooter();

  // --- helpers ---------------------------------------------------------------
  // Measure visibleRows for the screen band, apply follow-on-build, clamp the
  // viewport, and write selection/viewport into props. Call from buildScreen
  // right before screen.list(props).
  // hasSubtitle: rows carry a second (subtitle) text line, so on non-touch
  // hardware the denser override below uses the theme's *-with-subtitle row
  // height instead of its single-line one (see syncListViewport()).
  void syncListViewport(UiScreen& screen, freeink::ui::ListProps& props, bool hasSubtitle = false);

  // Kep con tro ve trong so dong dang co.
  //
  // ListNav::scrollBy() mang chu thich "clamp to range" nhung no chi kep `top`, con
  // `selected` di thang ra props. Man nao nap lai du lieu ma danh sach ngan di duoi chan
  // con tro dang nho thi chi so tro ra ngoai.
  //
  // Do 14/09/2026: hau qua chi la KHONG DONG NAO duoc to, vi ham list() chi DOI CHIEU
  // selectedIndex voi chi so dong chu khong lay no lam chi so mang, va duong bam co chot
  // chan rieng. Tuc loi tham my, khong phai doc ra ngoai bo nho.
  //

  // Queue a page move / a move to the first (last) visible row. The ring
  // variant (BoundaryFirstRing/BoundaryLastRing) addresses a row on the
  // 1..count ring tab screens use.
  void moveListPage(int direction);
  void moveToVisibleBoundary(bool last, bool ring = false);

  // --- shared state ----------------------------------------------------------
  // Selection + viewport (selected/top/visibleRows/followOnBuild). Access via
  // activeNav() in shared code; `nav` is the single-list default storage.
  freeink::ui::ListNav nav;
  ButtonNavigator buttonNavigator;

 private:
  static void screenTrampoline(UiScreen& screen, void* user);
  static void rowActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  // Named apart from UiAppHost::routeTouch so the host overload stays visible
  // (not name-hidden) to subclasses with extra touch surfaces.
  bool routeListTouch();

  // Apply one queued intent. Caller holds the render lock; tab intents are
  // handled by applyPendingNav() itself.
  bool applyNavIntent(NavIntent intent);
  bool applyPage(int direction);
  bool applyBoundary(bool last, bool ring);
  NavIntent popNavIntent();

  void drawPageHints();
  const bool wantsTouchLongPress;
  std::string pendingFavorite;
  std::string favoriteOrigin;
  bool activateFavorite = true;
  bool favoriteSaveFailed = false;
  int favoriteHintY = -1;
  NavIntent navQueue[NAV_QUEUE_SIZE];
  uint8_t navQueueHead = 0;
  uint8_t navQueueCount = 0;
  // A Select release held back while moves were still queued (see
  // confirmReleased), and a pin hold waiting for a pass that can take the lock.
  bool pendingConfirm = false;
  bool pendingPin = false;
};
