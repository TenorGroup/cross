// Activity infrastructure is stubbed; reservation and painting methods below
// are extracted from production and run with the shipped caption glyphs.
struct UiScreen {
  struct Rect { int y = 100, height = 650; } rect;
  const Rect& body() const { return rect; }
  void takeBottom(int16_t height) { rect.height -= height; }
};
namespace fui {
int listVisibleRows(const UiScreen::Rect& r, int rowHeight, int gap) { return r.height / (rowHeight + gap); }
}
struct UiAppHost {
  virtual void buildForTest() = 0;
  void renderUi() { buildForTest(); }
};
struct UiListActivity : UiAppHost {
  GfxRenderer& renderer;
  UiScreen screen;
  const char* hint;
  bool tabBandDrawn = false;
  int favoriteHintY = -1;
  struct Nav { int top = 0; int pageRows() const { return 1; } } nav;
  UiListActivity(GfxRenderer& r, const char* text) : renderer(r), hint(text) {}
  Nav& activeNav() { return nav; }
  int listCount() const { return 30; }
  bool supportsFavorites() const { return true; }
  const char* favoriteHintText() const { return hint; }
  virtual int favoriteHintLinesAbove() const { return 0; }
  void restorePinnedRows() {}
  void drawPageHints() {}
  void renderUi();
  void reserveFavoriteHint(UiScreen&);
  void reserveMoreBelowChevron(UiScreen&, int16_t, int);
  void buildForTest() override {
    screen = {};
    reserveFavoriteHint(screen);
    reserveMoreBelowChevron(screen, 40, 0);
  }
};
constexpr int STR_WAKE_POWER_HINT = 1;
struct SettingsActivity : UiListActivity {
  const char* wake;
  bool selectedWake = true;
  SettingsActivity(GfxRenderer& r, const char* h, const char* w) : UiListActivity(r, h), wake(w) {}
  bool showWakeHint() const { return selectedWake && !SETTINGS.hidden; }
  const char* tr(int) const { return wake; }
  int favoriteHintLinesAbove() const override;
  void drawWakeHint();
};
// ACTIVITY_FUNCTIONS

int checkWakeFooter() {
  int failures = 0, cases = 0;
  const char* captions[][2] = {
// CAPTIONS
  };
  for (int tier = 0; tier < 3; ++tier) for (int mode = 0; mode < 3; ++mode) {
    SETTINGS.uiTextSize = tier;
    SETTINGS.large = mode == 1;
    SETTINGS.tenorButtonSymbols = mode != 2;
    for (const auto& caption : captions) {
      GfxRenderer r;
      SettingsActivity activity(r, caption[1], caption[0]);
      activity.renderUi();
      int hintTop = 800, hintBottom = 0;
      for (size_t i = 0; i < r.ys.size(); ++i) {
        hintTop = std::min(hintTop, r.ys[i] + r.getTextInkTop(1, r.paintedText[i].c_str(), EpdFontFamily::REGULAR));
        hintBottom = std::max(hintBottom, r.ys[i] + r.getTextInkBottom(1, r.paintedText[i].c_str(), EpdFontFamily::REGULAR));
      }
      r.attemptedInkTop = 800;
      r.attemptedInkBottom = 0;
      activity.drawWakeHint();
      const auto check = [&](bool ok, const char* message) {
        if (!ok) {
          ++failures;
          std::printf("FAIL wake tier=%d mode=%d hint=%s bounds=[%d,%d) wakeTop=%d: %s\n",
                      tier, mode, caption[1], hintTop, hintBottom, r.attemptedInkTop, message);
        }
      };
      check(hintBottom > hintTop && r.attemptedInkBottom > r.attemptedInkTop, "both tips must have ink");
      check(hintBottom < r.attemptedInkTop, "upper tip needs at least one clear row before wake tip");
      check(r.lineBottom + 2 <= hintTop, "more-below arrow overlaps upper tip");
      check(activity.screen.body().y + activity.screen.body().height + 2 <= r.lineTop,
            "list body overlaps more-below arrow");
      // Changing focus must remove the extra reservation on the next frame.
      activity.selectedWake = false;
      r.ys.clear();
      activity.renderUi();
      GfxRenderer ordinary;
      tenorchrome::drawTip(ordinary, caption[1]);
      check(r.ys == ordinary.ys, "focus change kept stale wake-tip offset");
      ++cases;
    }
  }
  SETTINGS = {};
  std::printf("wake footer: %d cases, %d failures (VI/EN glyphs; CJK panel acceptance open)\n", cases, failures);
  return failures;
}
