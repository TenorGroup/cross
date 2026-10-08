from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
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


class OverlaySnapshotCacheTest(unittest.TestCase):
    def test_clean_and_dirty_snapshots_release_cache_before_allocation(self):
        source = SOURCE.read_text()
        release = body(source, "void EpubReaderActivity::releaseTextCachesBeforeOverlaySnapshot()")
        snapshots = []
        for signature in ("void EpubReaderActivity::renderBook()",
                          "bool EpubReaderActivity::renderPreview("):
            overlay = body(body(source, signature), "if (overlay != Overlay::None && usesToolbarMenu())")
            snapshots.append(overlay[:overlay.index("renderer.storeBwBuffer();") + len("renderer.storeBwBuffer();")])
        opening = body(source, "void EpubReaderActivity::openOverlay(").split("RenderLock lock;", 1)[1]
        snapshots.append(body(opening, "if (previous == Overlay::None)"))
        harness = r"""
#include <cstdio>
struct FontCacheManager {
  bool released = false;
  void releaseSdFontCaches() { released = true; }
};
struct Renderer {
  FontCacheManager* cache;
  FontCacheManager* getFontCacheManager() { return cache; }
  bool storeBwBuffer() { return !cache || cache->released; }
};
struct EpubReaderActivity {
  Renderer renderer;
  bool textSettingsDirty;
  bool overlayPageStored = false;
  void releaseTextCachesBeforeOverlaySnapshot();
  @SNAPSHOTS@
};
void EpubReaderActivity::releaseTextCachesBeforeOverlaySnapshot() { @RELEASE@ }
int main() {
  int failed = 0;
  using Snapshot = void (EpubReaderActivity::*)();
  Snapshot snapshots[] = {&EpubReaderActivity::snapshot0, &EpubReaderActivity::snapshot1,
                          &EpubReaderActivity::snapshot2};
  for (int route = 0; route < 3; ++route) {
    for (bool dirty : {false, true}) {
      FontCacheManager cache;
      EpubReaderActivity reader{{&cache}, dirty};
      (reader.*snapshots[route])();
      if (!reader.overlayPageStored || !cache.released || reader.textSettingsDirty != dirty) {
        std::fprintf(stderr, "snapshot route=%d dirty=%d failed\n", route, dirty);
        ++failed;
      }
    }
    EpubReaderActivity reader{{nullptr}, false};
    (reader.*snapshots[route])();
    if (!reader.overlayPageStored) ++failed;
  }
  return failed;
}
"""
        harness = "#include <initializer_list>\n" + harness
        harness = harness.replace("@RELEASE@", release).replace(
            "@SNAPSHOTS@", "\n".join("void snapshot%d() {%s}" % (index, snapshot)
                                      for index, snapshot in enumerate(snapshots)))
        with tempfile.TemporaryDirectory(prefix="overlay-snapshot-") as directory:
            cpp = Path(directory) / "cache-release.cpp"
            binary = Path(directory) / "cache-release"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
