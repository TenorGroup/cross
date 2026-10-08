from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO = Path(os.environ.get("OVERLAY_SNAPSHOT_REPO", Path(__file__).resolve().parents[2]))
SOURCE = REPO / "src/activities/reader/EpubReaderActivity.cpp"


def body(source, signature):
    start = source.index("{", source.index(signature))
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:end]
    raise AssertionError("Unterminated body: " + signature)


class PreviewPageLifetimeTest(unittest.TestCase):
    def test_preview_allocation_releases_stale_snapshot(self):
        production = body(SOURCE.read_text(), "bool EpubReaderActivity::renderPreview(")
        harness = r"""
#include <atomic>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>
struct ReaderRenderSpec {};
struct Settings {
  ReaderRenderSpec readerRenderSpec(int, int) { return {}; }
} SETTINGS;
unsigned millis() { return 37; }
struct Page {
  unsigned visibleTextOffset = 4516;
  std::vector<int> footnotes{1, 2};
  std::vector<int> links{3};
};
struct Renderer {
  bool snapshotHeld = false;
  int clears = 0;
  int stores = 0;
  void clearScreen() { ++clears; }
  bool storeBwBuffer() { ++stores; snapshotHeld = true; return true; }
};
struct Section {
  static bool returnPage;
  static bool previewSawSnapshot;
  Renderer& renderer;
  Section(int, int, Renderer& r, bool) : renderer(r) {}
  bool isBuilding() { return false; }
  bool isBuildParked() { return true; }
  bool parkBuild() { return true; }
  std::unique_ptr<Page> previewPage(const ReaderRenderSpec&, unsigned) {
    previewSawSnapshot = renderer.snapshotHeld;
    return returnPage ? std::make_unique<Page>() : nullptr;
  }
};
bool Section::returnPage = false;
bool Section::previewSawSnapshot = false;
struct Popup {
  bool isActive() { return false; }
  void render(Renderer&) {}
};
struct Gpio { bool isUsbConnected() { return true; } } gpio;
struct EpubReaderActivity {
  enum class Overlay { None, Toolbar };
  Overlay overlay = Overlay::None;
  std::unique_ptr<Section> catchUp;
  Renderer renderer;
  int buildViewportWidth = 518, buildViewportHeight = 759, currentSpineIndex = 1, epub = 1;
  bool preview = false, overlayPageStored = false, pageFrameUsb = false, pageFrameShown = false;
  bool dropPaint = false;
  unsigned xemTruocDich = 4516, currentPageVisibleOffset = 0, lastRenderCompleteMs = 0;
  std::vector<int> currentPageFootnotes, currentPageLinks;
  int currentPageLinkMarginLeft = 0, currentPageLinkMarginTop = 0;
  int discarded = 0, rendered = 0, overlays = 0, pushed = 0, cacheReleased = 0;
  std::atomic<bool> paintDropped{false};
  Popup overlayPopup;
  void dropCatchUp() { catchUp.reset(); }
  void settleBuildPopup() {}
  void discardOverlayPage() {
    if (overlayPageStored) { ++discarded; renderer.snapshotHeld = false; overlayPageStored = false; }
  }
  void renderContents(std::unique_ptr<Page>, int, int, int, int) {
    ++rendered; paintDropped.store(dropPaint);
  }
  bool usesToolbarMenu() { return true; }
  void releaseTextCachesBeforeOverlaySnapshot() { ++cacheReleased; }
  void renderOverlay() { ++overlays; }
  void pushOverlayRefresh() { ++pushed; }
  bool renderPreview(int, int, int, int);
};
bool EpubReaderActivity::renderPreview(int marginTop, int marginRight, int marginBottom, int marginLeft) {
@PRODUCTION@
}
int main() {
  int failed = 0, cases = 0;
  for (bool held : {false, true}) for (bool success : {false, true})
    for (bool overlay : {false, true}) for (bool dropped : {false, true}) {
      ++cases;
      EpubReaderActivity reader;
      reader.overlay = overlay ? EpubReaderActivity::Overlay::Toolbar : EpubReaderActivity::Overlay::None;
      reader.renderer.snapshotHeld = reader.overlayPageStored = held;
      reader.dropPaint = dropped;
      Section::returnPage = success;
      Section::previewSawSnapshot = false;
      const bool result = reader.renderPreview(1, 2, 3, 4);
      const bool pushed = success && overlay && !dropped;
      const bool ok = !Section::previewSawSnapshot && result == success &&
          reader.discarded == int(held) && reader.rendered == int(success) &&
          reader.renderer.clears == int(success) && reader.pushed == int(pushed) &&
          reader.renderer.snapshotHeld == pushed && reader.overlayPageStored == pushed &&
          reader.overlays == int(pushed) && reader.cacheReleased == int(pushed) &&
          reader.pageFrameShown == (success && !dropped) &&
          (!success || reader.currentPageVisibleOffset == 4516);
      if (!ok) {
        std::fprintf(stderr, "held=%d success=%d overlay=%d dropped=%d: preview_snapshot=%d failed\n",
                     held, success, overlay, dropped, Section::previewSawSnapshot);
        ++failed;
      }
    }
  std::printf("preview-lifetime cases=%d failures=%d\n", cases, failed);
  return failed ? 1 : 0;
}
""".replace("@PRODUCTION@", production)
        with tempfile.TemporaryDirectory(prefix="preview-lifetime-") as directory:
            cpp = Path(directory) / "preview.cpp"
            binary = Path(directory) / "preview"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
