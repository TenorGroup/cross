#pragma once

#include <functional>
#include <string>

#include "Icon.h"
#include "components/UiAppHost.h"
#include "components/lists/list.h"

class GfxRenderer;
class MappedInputManager;

namespace readerugly {
void text(const GfxRenderer& renderer, const freeink::ui::Rect& rect, const char* label,
          freeink::ui::TextAlign align = freeink::ui::TextAlign::Left);
void paper(const GfxRenderer& renderer, const freeink::ui::Rect& rect);
void selected(const GfxRenderer& renderer, const freeink::ui::Rect& rect);
}

// FreeInkUI chrome for the toolbar reader menu (Settings -> Reader -> Reader
// Menu Style -> Toolbar), painted over the page that is already on screen:
//
//  - Toolbar: a bottom sheet holding a chapter scrub row (< capsule >) and a
//    Contents / Text / More tile row; the page above it stays untouched.
//  - Panel: a taller bottom sheet with a title, a paged row list (name left,
//    value right) and the same tile row as a switcher.
//
// EpubReaderActivity owns the state (which overlay, selection, row contents)
// and the physical-button handling; this class owns the layout, the tap
// targets FreeInkUI registers for it, and the translation of dispatched
// actions back into the small event set the reader acts on. Nothing here
// clears the screen: the page stays visible around the chrome.
class ReaderToolbarUi : public UiAppHost {
 public:
  enum class Event { None = 0, Dismiss = 1, Tool = 2, PrevChapter = 3, NextChapter = 4, Scrub = 5, Row = 6,
                     SizeStep = 8, SizeEntry = 9, SpacingDraft = 10, SpacingCommit = 11, NumericKey = 12 };
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  enum class TextView : uint8_t { None, Rows, Fonts, Spacing, PointSize };
#endif
  // The tabs: 0 Contents, 1 Text, 2 More, 3 Favorites.
  static constexpr int kToolCount = 4;

  struct Model {
    bool panel = false;  // false = toolbar, true = a Contents/Text/More panel
    freeink::ui::Insets footerInsets{};  // Physical button chrome, rotated into logical coordinates.
    // Toolbar
    const char* chapterTitle = nullptr;
    const char* pageInfo = nullptr;  // "12/40   51%"
    int progressPermille = 0;        // 0..1000 book progress (scrub handle)
    // Panel
    const char* panelTitle = nullptr;
    int itemCount = 0;
    int selectedIndex = -1;  // row the buttons' cursor sits on; -1 = none shown
    // Rows the sheet is sized to; 0 = from the list. A deeper level of a panel (the Text panel's
    // font list) sets the rows of the panel it came from so the frame does not change.
    int sheetRows = 0;
    std::function<std::string(int)> rowText;
    std::function<std::string(int)> rowValue;
    // Optional: the row in use (the font in the family list). Drawn in bold with a tick at the row end.
    std::function<bool(int)> rowMarked;
    // A row pinned to Favorites: the small heart, touch before its value, buttons after its name.
    std::function<bool(int)> rowPinned;
    // Drawn in the list's place when it has no rows (an empty Favorites).
    const char* emptyText = nullptr;
    // Buttons: a row that opens a list or another screen ends in the grey chevron, as in Settings.
    std::function<bool(int)> rowOpens;
    // Tile row: the tool in focus (toolbar) / the open panel (panel). 0..3, -1 none.
    int activeTool = 0;
    // Button boards keep the theme's denser list row height (as every other
    // list does there); touch boards use FreeInkUI's finger-sized rows.
    bool denseRows = false;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    TextView textView = TextView::None;
    int spacingPlace = 2, spacingDraftPermille = 500;
    std::function<const char*(int)> spacingLabel;
    const char* numericDraft = nullptr;
    const char* numericHint = nullptr;
#endif
  };

  struct Routed {
    Event event = Event::None;
    int value = 0;        // Tool: tool index; Row: row index
    int permille = -1;    // Scrub: 0..1000 along the track
    bool routed = false;  // the gate was open and a touch frame was routed
    int x = 0;            // touch position of the routed frame (logical px)
    int y = 0;
    bool hold = false;    // Row: a long press (X4 Pro: pin or unpin the row)
  };

  explicit ReaderToolbarUi(GfxRenderer& renderer);

  // Screen-entry reset + action wiring. Call once when the overlay opens.
  void begin();
  // What the next render() draws. The strings must stay alive until render()
  // returns; the row callbacks are invoked during render() for the visible rows.
  void setModel(const Model& model) { model_ = model; }
  // Paint into the framebuffer (no refresh, no clear). Run from the render task.
  void render();
  // Route one loop-task input frame; returns the action it mapped to, if any.
  Routed route(const MappedInputManager& input);

  // Panel list selection/viewport, shared with the reader's input handling.
  // The same fui::ListNav the list-menu screens use: scrollBy() pages the
  // viewport (measured page size, no-op detection), the top/selected fields
  // are the live state, and buildPanel() syncs it into the list each build.
  freeink::ui::ListNav& nav() { return nav_; }
  // Rows one page holds, measured after the first render.
  int visibleRows() const { return nav_.pageRows(); }
  // Buttons: the rows the last panel sheet was sized to (its frame), glimpse row included.
  int sheetRows() const { return sheetRows_; }
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  int scrollRows(const MappedInputManager& input, int count) const;
#endif

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void buildToolbar(UiScreen& screen);
  void buildPanel(UiScreen& screen);
  void buildSheet(UiScreen& screen, const freeink::ui::SheetProps& props, int16_t height);
  void buildToolRow(UiScreen& screen, freeink::ui::LayoutAnchor anchor, int16_t sideInset);
  void paintUgly();
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  void fadeMoreBelow();
#endif
  int16_t fadeRight_ = 0;  // buttons: the fade under the last full row stops short of the scroll bar
  int sheetRows_ = 0;
  GfxRenderer* renderer_ = nullptr;
  freeink::ui::Rect skinFrame_{}, skinList_{}, skinMeta_{};

  Model model_;
  Routed pending_;
  freeink::ui::ListNav nav_;

  // Row window materialised for the visible page only (a contents list runs to
  // hundreds of entries; labels are copied here so ListItem can point at them).
  static constexpr int kMaxWindow = 16;
  std::string windowLabels_[kMaxWindow];
  std::string windowValues_[kMaxWindow];
  bool markedLabels_[kMaxWindow] = {};
  freeink::ui::ListItem windowItems_[kMaxWindow];
  // fui::ButtonProps / ListProps / HeaderProps embed a 324-byte StyleSet: keep
  // them off the stack (locals stay under 256 bytes).
  freeink::ui::ButtonProps stepProps_;
  freeink::ui::ListProps listProps_;
  freeink::ui::Rect pageIndicatorRect_{};
  void drawMarkedRows(UiScreen& screen, const freeink::ui::Rect& listRect, int16_t rowH, int16_t rowGap, int windowCount);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  void buildX4Toolbar(UiScreen& screen);
  void buildX4Panel(UiScreen& screen);
  void buildX4Tools(UiScreen& screen);
  void buildX4Spacing(UiScreen& screen, const freeink::ui::Rect& frame);
  void buildX4Keypad(UiScreen& screen, const freeink::ui::Rect& frame);
#endif
};
