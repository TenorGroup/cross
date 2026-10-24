"""Run with python3 test/test_list_viewport.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# Compile the production method without the activity's hardware dependencies.
source = (ROOT / "src/activities/UiListActivity.cpp").read_text()
method = source.split("void UiListActivity::syncListViewport(", 1)[1]
method = "void UiListActivity::syncListViewport(" + method.split(
    "\nvoid UiListActivity::keepViewportOnVisibleSelection", 1
)[0]
helper = "void UiListActivity::keepViewportOnVisibleSelection(" + source.split(
    "void UiListActivity::keepViewportOnVisibleSelection(", 1
)[1].split("\nvoid UiListActivity::renderUi", 1)[0]
tab_source = (ROOT / "src/activities/UiTabListActivity.cpp").read_text()
tab_method = "void UiTabListActivity::syncTabListViewport(" + tab_source.split(
    "void UiTabListActivity::syncTabListViewport(", 1
)[1].split("\nUiTabListActivity::CuaSoThe", 1)[0]
harness = r"""
#include <components/lists/list.h>
#include <cassert>
#include <algorithm>
namespace fui = freeink::ui;
// The production method needs the shell around it; every member it touches is stubbed here and the
// list state itself is the real ListNav::syncToProps from the SDK.
struct UiScreen {
  int16_t height = 80;
  struct Theme { int16_t rowHeight = 20; int16_t listRowGap = 0; };
  Theme theme() const { return {}; }
  fui::Rect body() const { return {0, 0, 200, height}; }
  fui::ListProps resolveListProps(const fui::ListProps& props) const { return props; }
};
struct Mapped { bool touch = true; bool hasTouch() const { return touch; } };
struct Metrics { int listRowHeight = 20, listWithSubtitleRowHeight = 30; };
struct UITheme {
  static UITheme& getInstance() { static UITheme theme; return theme; }
  Metrics getMetrics() const { return {}; }
};
constexpr int TENOR_PILL_ROW_PADDING_Y = 4;
namespace tenorchrome { constexpr bool kTouchShell = false; }
struct UiListActivity {
  fui::ListNav nav;
  int count = 16;
  Mapped mappedInput;
  bool rowsFramed = false;
  int pageAnchorRow = -1;
  fui::ListNav& activeNav() { return nav; }
  int listCount() const { return count; }
  static int kepConTro(int chon, int soDong) { return soDong <= 0 || chon < 0 ? 0 : std::min(chon, soDong - 1); }
  void frameRows(fui::ListProps&) {}
  void reserveFixedMenuContent(UiScreen&) {}
  void reserveFavoriteHint(UiScreen&) {}
  void decoratePinnedRows(fui::ListProps&) {}
  void reserveRowFrame(UiScreen&, int) {}
  void keepUglyRows(const fui::ListProps&) {}
  void measurePageScrollbar(UiScreen&, const fui::ListProps&) {}
  void syncListViewport(UiScreen&, fui::ListProps&, bool = false);
  void keepViewportOnVisibleSelection(fui::ListProps&, int, bool, int);
};
struct UiTabListActivity : UiListActivity {
  void clampAfterNav() {}
  void syncTabListViewport(UiScreen&, fui::ListProps&, bool = false);
};
""" + method + helper + tab_method + r"""
template <typename Activity, typename Sync>
void scenario(int offset, Sync sync) {
  Activity activity;
  UiScreen screen;
  fui::ListProps props;
  auto& nav = activity.nav;
  nav.reset(offset);
  sync(activity, screen, props);
  nav.onListRendered(0, 4, true);
  nav.requestScroll(4);
  sync(activity, screen, props);
  assert(nav.top == 4 && props.topIndex == 4 && nav.selected == offset);
  nav.onListRendered(4, 4, false);
  nav.selected = 5 + offset;
  nav.requestScroll(-4);
  sync(activity, screen, props);
  assert(nav.top == 0 && props.topIndex == 0 && nav.selected == 5 + offset);

  // Wrapped settings rows: button follow must preserve the measured viewport
  // even when the fixed-height estimate says the entire list fits.
  activity.count = 6;
  screen.height = 160;
  nav.onListRendered(2, 3, true);
  nav.drawnCount = 6;
  nav.requestSelection(3 + offset);
  sync(activity, screen, props);
  assert(nav.top == 2 && props.topIndex == 2);
}
int main() {
  scenario<UiListActivity>(0, [](UiListActivity& a, UiScreen& s, fui::ListProps& p) { a.syncListViewport(s, p); });
  // The tab list keeps its ring entry (the tab bar) in front of the rows.
  scenario<UiTabListActivity>(1, [](UiTabListActivity& a, UiScreen& s, fui::ListProps& p) { a.syncTabListViewport(s, p); });
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "viewport.cpp"
    executable = Path(directory) / "viewport"
    cpp.write_text("#include <initializer_list>\n" + harness)
    subprocess.run([
        "c++", "-std=c++17", "-I",
        str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/include"),
        str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp"),
        str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("List viewport regression checks passed")
