"""Run the reader's return stack methods with bounded card and layout substitutes."""
import re
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_reader_menu_runtime_v1052 import function

REPO = Path(__file__).resolve().parents[2]


def run_case(body):
    text = Path(os.environ.get('T4_READER_CPP', REPO / 'src/activities/reader/EpubReaderActivity.cpp')).read_text()
    header = (REPO / 'src/activities/reader/EpubReaderActivity.h').read_text()
    saved = re.search(r'  struct SavedPosition \{.*?\n  \};', header, re.S).group(0)
    names = ['navigateToHref', 'restoreSavedPosition', 'saveLinkStack', 'loadLinkStack',
             'jumpToQuoteForEdit', 'jumpToPercent']
    if 'void EpubReaderActivity::pushReturnPosition()' in text:
        names += ['pushReturnPosition']
    # A wake pull that moved the place puts the page the reader was on on the back-stack. Absent in a
    # source that does not have it yet: an empty body stands in, so the cases below fail on what they assert.
    pulled = ''
    for name in ['pushSavedPosition', 'pushPulledReturn']:
        if f'EpubReaderActivity::{name}(' in text:
            names.append(name)
        elif name == 'pushPulledReturn':
            pulled = 'void EpubReaderActivity::pushPulledReturn() {}\n'
    for name in ['hasReturnPosition', 'returnPageNumber', 'chapterTargetDiffers']:
        if f'EpubReaderActivity::{name}(' in text:
            names.append(name)
    methods = '\n'.join(function(text, 'EpubReaderActivity', name) for name in names)
    start = text.index('  auto progressChangeResultHandler = [this]')
    end = text.index('\n  };', start) + len('\n  };')
    progress = ('void EpubReaderActivity::changeProgress(const ActivityResult& result) {\n' +
                text[start:end] + '\nprogressChangeResultHandler(result);\n}\n')
    start = text.index('  const auto gotoSpine = [this]')
    end = text.index('\n  };', start) + len('\n  };')
    jump = text[start:end]
    call = 'gotoSpine(target, saveReturn);' if 'saveReturn' in jump else '(void)saveReturn; gotoSpine(target);'
    toolbar = ('void EpubReaderActivity::toolbarJump(int target, bool saveReturn) {\n' +
               jump + '\n' + call + '\n}\n')
    namespace = text[text.index('namespace {\n// The back-stack'):text.index('void EpubReaderActivity::saveLinkStack')]
    source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define TRACE_JUMP_BEGIN(...) ((void)0)
int clampPercent(int percent) { return std::clamp(percent, 0, 100); }
uint32_t readerLayoutKeyFor(const uint8_t fontSize, const uint16_t width = 100, const uint16_t height = 100) {
  uint32_t hash = 2166136261u;
  const uint32_t values[] = {1u, fontSize, width, height, 1u, 1u, 0u, 0u};
  for (const uint32_t value : values) hash = (hash ^ value) * 16777619u;
  return hash;
}
std::map<std::string, std::vector<uint8_t>> files;
struct HalFile {
  std::string path;
  int read(uint8_t* data, size_t count) {
    const auto& bytes = files[path];
    count = std::min(count, bytes.size());
    std::memcpy(data, bytes.data(), count);
    return count;
  }
  size_t write(const uint8_t* data, size_t count) {
    files[path].assign(data, data + count);
    return count;
  }
  size_t size() const { return files[path].size(); }
  void close() {}
};
struct {
  bool exists(const char* path) { return files.count(path); }
  bool openFileForRead(const char*, const std::string& path, HalFile& f) {
    f.path = path; return files.count(path);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& f) {
    f.path = path; return true;
  }
  void remove(const char* path) { files.erase(path); }
  bool rename(const char* from, const char* to) {
    files[to] = files[from]; files.erase(from); return true;
  }
} Storage;
struct {
  bool isSleepTransition() const { return true; }
  void deferWrite(void (*write)()) { write(); }
} activityManager;
struct RenderLock {};
namespace koautosync {
struct PullChange {
  std::string path;
  int fromSpineIndex = 0, fromPage = 0, toSpineIndex = 0, toPage = 0;
  uint32_t fromOffset = 0;
  bool hasFromOffset = false;
};
PullChange pulledChange;
bool hasPulledChange = false;
const PullChange* progressJustPulled() { return hasPulledChange ? &pulledChange : nullptr; }
void acknowledgeProgressPulled() { hasPulledChange = false; }
}  // namespace koautosync
struct Epub {
  std::string getCachePath() const { return "cache"; }
  int getSpineItemsCount() const { return 10; }
  int resolveHrefToSpineIndex(const std::string&) const { return 1; }
  size_t getBookSize() const { return 10000; }
  int getSpineIndexForSize(size_t size) const { return size < 5000 ? 0 : 1; }
  size_t getCumulativeSpineItemSize(int spine) const { return 5000 * (spine + 1); }
};
struct QuoteRecord {
  std::string path = "book";
  int spine = 1, page = 0;
  bool hasAnchor = false;
  uint32_t anchorStart = 0;
} quote;
namespace quotes {
bool load(const std::string&, QuoteRecord& result) { result = quote; return true; }
}
struct ProgressChangeResult {
  int spineIndex = 1, page = 5, totalPages = 40;
  bool hasVisibleTextOffset = false, hasSavedProgress = false;
  uint32_t visibleTextOffset = 0;
  std::string xpath;
  float percentage = 0;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<ProgressChangeResult> data;
};
struct CrossPointPosition { int spineIndex = 1, pageNumber = 5; };
struct SavedProgress { std::string xpath; float percentage; };
struct ProgressMapper {
  static CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>&, SavedProgress, int, int, int) { return {}; }
};
struct Section {
  int currentPage = 12, pageCount = 40, charsPerPage = 100;
  bool building = false;
  uint32_t reached = 0;
  mutable int lookups = 0;
  bool isBuilding() const { return building; }
  bool buildReachedVisibleTextOffset(uint32_t offset) const { return building && offset <= reached; }
  Section() = default;
  Section(const std::shared_ptr<Epub>&, int, int) {}
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) const {
    return page < pageCount ? std::optional<uint32_t>(page * charsPerPage) : std::nullopt;
  }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset) const {
    ++lookups;
    return offset / charsPerPage < static_cast<uint32_t>(pageCount) ?
        std::optional<uint16_t>(offset / charsPerPage) : std::nullopt;
  }
  bool laidOutTo(float) const { return true; }
  uint16_t pageAtFraction(float fraction) const { return std::min(int(fraction * pageCount), pageCount - 1); }
  int estimatedTotalPages() const { return pageCount; }
  std::optional<uint16_t> findAnchor(const std::string& anchor) const {
    if (anchor == "here") return 12;
    if (anchor == "elsewhere") return 15;
    return std::nullopt;
  }
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const { return findAnchor(anchor); }
};
struct EpubReaderActivity {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  int currentSpineIndex = 0, nextPageNumber = 0, cachedSpineIndex = 0;
  std::optional<uint32_t> cachedVisibleTextOffset, pendingOffsetJump;
  std::string pendingAnchor;
  std::string bookPath = "book", pendingQuoteEdit;
  bool quoteEditPageShown = false, pendingPercentJump = false;
  float pendingSpineProgress = 0;
  int renderer = 0, cachedChapterTotalPageCount = 40;
''' + saved + r'''
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  uint8_t fontSize = 16;
  uint32_t currentLayoutKey = readerLayoutKeyFor(fontSize);
  bool returnPushedForLoad = false;
  bool linkStackOnCard = false;
  void applyTextSetting(uint8_t size) {
    fontSize = size;
    currentLayoutKey = readerLayoutKeyFor(fontSize);
  }
  void clearDeferredReposition() { cachedVisibleTextOffset.reset(); }
  void requestUpdate() {}
  void navigateToHref(const std::string&, bool);
  void restoreSavedPosition();
  void saveLinkStack() const;
  void loadLinkStack();
  void pushReturnPosition();
  void pushSavedPosition(const SavedPosition&);
  void pushPulledReturn();
  void jumpToQuoteForEdit(const std::string&);
  void jumpToPercent(int);
  void loadCachedBookmarks() {}
  void openReaderMenu() {}
  void changeProgress(const ActivityResult&);
  void toolbarJump(int, bool);
  bool hasReturnPosition() const;
  int returnPageNumber() const;
  bool chapterTargetDiffers(int, const std::string&) const;
};
''' + namespace + methods + pulled + progress + toolbar + '\nint main() {\n' + body + '\n}\n'
    with tempfile.TemporaryDirectory(prefix='cross-return-runtime-') as folder:
        cpp, program = Path(folder) / 'test.cpp', Path(folder) / 'test'
        cpp.write_text(source)
        result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                 '-Wno-unused-variable', '-fsanitize=address,undefined', str(cpp), '-o', str(program)],
                                capture_output=True, text=True)
        if result.returncode:
            return result
        return subprocess.run([str(program)], capture_output=True, text=True)


class ReturnStackRuntimeTest(unittest.TestCase):
    def check(self, body):
        result = run_case(body)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


    def test_reflow_uses_the_current_map_and_back_keeps_the_original_character(self):
        self.check(r"""
  EpubReaderActivity reader;
  reader.section->currentPage = 14;
  reader.navigateToHref("c2.xhtml", true);
  assert(reader.savedPositions[0].textOffset == 1400);
  assert(reader.returnPageNumber() == 15);
  reader.section = std::make_unique<Section>();
  reader.section->charsPerPage = 65;
  assert(reader.returnPageNumber() == 15);  // another chapter, unchanged layout
  reader.currentSpineIndex = 0;
  reader.applyTextSetting(18);  // the reader text-size control changes the layout key
  assert(reader.returnPageNumber() == 22);
  assert(reader.savedPositions[0].pageNumber == 14);

  reader.currentLayoutKey = reader.savedPositions[0].layoutKey;
  reader.restoreSavedPosition();
  assert(reader.pendingOffsetJump == 1400 && reader.currentSpineIndex == 0);
  reader.section = std::make_unique<Section>();
  reader.section->charsPerPage = 65;
  assert(reader.section->getPageForVisibleTextOffset(*reader.pendingOffsetJump) == 21);
""")

    def test_saved_key_survives_reopen_and_reflows_after_external_text_setting_change(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.section->currentPage = 14;
  reader.navigateToHref("c2.xhtml", true);
  reader.saveLinkStack();
  assert(files["cache/links.bin"].size() == 14);

  EpubReaderActivity reopened;
  reopened.loadLinkStack();
  assert(reopened.returnPageNumber() == 15);
  reopened.currentSpineIndex = 0;
  reopened.section->charsPerPage = 65;
  reopened.applyTextSetting(18);  // Settings changed while the book was closed.
  assert(reopened.returnPageNumber() == 22);
  assert(reopened.savedPositions[0].pageNumber == 14);
''')

    def test_current_map_changes_the_label_without_changing_the_saved_page(self):
        self.check(r"""
  EpubReaderActivity reader;
  reader.pushSavedPosition({0, 14, 1400, readerLayoutKeyFor(16)});
  reader.applyTextSetting(18);
  reader.section->charsPerPage = 65;
  assert(reader.returnPageNumber() == 22);
  assert(reader.savedPositions[0].pageNumber == 14);
""")

    def test_missing_or_unreached_map_never_uses_the_old_page(self):
        self.check(r"""
  EpubReaderActivity reader;
  reader.pushSavedPosition({0, 14, 1400, readerLayoutKeyFor(16)});
  reader.applyTextSetting(18);
  reader.section->building = true;
  reader.section->reached = 1000;
  assert(reader.returnPageNumber() == -1);
  assert(reader.section->lookups == 0);
  reader.section->charsPerPage = 65;
  reader.section->reached = 2000;
  assert(reader.returnPageNumber() == 22);
  reader.section->pageCount = 0;
  assert(reader.returnPageNumber() == -1);
  reader.section.reset();
  assert(reader.returnPageNumber() == -1);
  reader.restoreSavedPosition();
  assert(reader.pendingOffsetJump == 1400);
""")

    def test_legacy_stack_has_a_back_target_without_a_page_label(self):
        self.check(r"""
  EpubReaderActivity reader;
  reader.pushSavedPosition({0, 14, UINT32_MAX, readerLayoutKeyFor(15)});
  assert(reader.hasReturnPosition() && reader.returnPageNumber() == -1);
  assert(reader.section->lookups == 0);
  reader.restoreSavedPosition();
  assert(reader.nextPageNumber == 14 && !reader.pendingOffsetJump);
""")

    def test_saved_character_drives_return_after_layout_changes(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.navigateToHref("c2.xhtml", true);
  reader.restoreSavedPosition();
  assert(reader.currentSpineIndex == 0 && reader.nextPageNumber == 12);
  assert(reader.pendingOffsetJump == 1200);
''')

    def test_released_section_keeps_the_character_cached_before_the_menu(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.section.reset();
  reader.nextPageNumber = 12;
  reader.cachedVisibleTextOffset = 1200;
  reader.navigateToHref("c2.xhtml", true);
  reader.restoreSavedPosition();
  assert(reader.pendingOffsetJump == 1200);
''')

    def test_missing_character_keeps_the_old_page_fallback(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.section.reset();
  reader.nextPageNumber = 12;
  reader.navigateToHref("c2.xhtml", true);
  reader.restoreSavedPosition();
  assert(reader.currentSpineIndex == 0 && reader.nextPageNumber == 12);
  assert(!reader.pendingOffsetJump);
''')

    def test_sleep_file_has_a_version_and_preserves_the_character(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.navigateToHref("c2.xhtml", true);
  reader.saveLinkStack();
  assert(files["cache/links.bin"].size() == 14);
  assert(files["cache/links.bin"][0] == 2 && files["cache/links.bin"][1] == 1);
  EpubReaderActivity reopened;
  reopened.loadLinkStack();
  reopened.restoreSavedPosition();
  assert(reopened.pendingOffsetJump == 1200);
''')

    def test_old_files_keep_the_page_for_all_three_depths(self):
        self.check(r'''
  for (int depth = 1; depth <= 3; ++depth) {
    auto& bytes = files["cache/links.bin"];
    bytes = {static_cast<uint8_t>(depth)};
    for (int i = 0; i < depth; ++i) bytes.insert(bytes.end(), {static_cast<uint8_t>(i), 0, 12, 0});
    EpubReaderActivity reader;
    reader.loadLinkStack();
    assert(reader.footnoteDepth == depth);
    reader.restoreSavedPosition();
    assert(reader.currentSpineIndex == depth - 1 && reader.nextPageNumber == 12);
    assert(!reader.pendingOffsetJump);
  }
''')

    def test_fourth_jump_discards_the_oldest_entry(self):
        self.check(r'''
  EpubReaderActivity reader;
  for (int i = 0; i < 4; ++i) {
    reader.currentSpineIndex = i;
    reader.section = std::make_unique<Section>();
    reader.navigateToHref("c2.xhtml", true);
  }
  assert(reader.footnoteDepth == 3);
  assert(reader.savedPositions[0].spineIndex == 1);
  for (int i = 3; i >= 1; --i) {
    reader.restoreSavedPosition();
    assert(reader.currentSpineIndex == i);
  }
  assert(reader.footnoteDepth == 0);
''')

    def test_unknown_truncated_or_oversized_files_are_rejected(self):
        self.check(r'''
  for (const auto& bytes : std::vector<std::vector<uint8_t>>{
      {2,1,0,0,12,0,176,4,0,0}, {1,1,0,0,12,0},
      {1,1,0,0,12,0,176,4,0,0,0}, {1,1,10,0,12,0,176,4,0,0}}) {
    files["cache/links.bin"] = bytes;
    EpubReaderActivity reader;
    reader.loadLinkStack();
    assert(reader.footnoteDepth == 0);
  }
''')

    def test_quote_and_percent_jumps_save_once_and_same_page_saves_nothing(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.jumpToQuoteForEdit("saved");
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  assert(reader.pendingOffsetJump == 1200);
  reader.section = std::make_unique<Section>();
  quote.spine = 0;
  quote.hasAnchor = true;
  quote.anchorStart = 1240;
  reader.jumpToQuoteForEdit("saved");
  assert(reader.footnoteDepth == 0);
  quote.anchorStart = 1350;
  reader.jumpToQuoteForEdit("saved");
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  reader.section = std::make_unique<Section>();
  reader.jumpToPercent(15);
  assert(reader.footnoteDepth == 0);
  reader.section = std::make_unique<Section>();
  reader.jumpToPercent(80);
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  assert(reader.currentSpineIndex == 0 && reader.pendingOffsetJump == 1200);
''')

    def test_bookmark_and_anchored_progress_jumps_only_save_when_the_page_changes(self):
        self.check(r'''
  EpubReaderActivity reader;
  ProgressChangeResult progress;
  progress.spineIndex = 0;
  progress.page = 12;
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 0);
  progress.page = 15;
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  assert(reader.pendingOffsetJump == 1200);
  reader.section = std::make_unique<Section>();
  progress.hasVisibleTextOffset = true;
  progress.visibleTextOffset = 1240;
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 0);
  progress.visibleTextOffset = 1550;
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  reader.section = std::make_unique<Section>();
  progress.spineIndex = 1;
  reader.changeProgress({true, progress});
  assert(reader.footnoteDepth == 0);
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  reader.section.reset();
  progress.hasVisibleTextOffset = false;
  progress.page = 15;
  reader.nextPageNumber = 12;
  reader.changeProgress({false, progress});
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
''')

    def test_scrub_remembers_the_origin_and_chapter_steps_do_not(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.toolbarJump(0, true);
  assert(reader.footnoteDepth == 0);
  reader.toolbarJump(3, true);
  assert(reader.footnoteDepth == 1);
  reader.toolbarJump(3, true);
  assert(reader.footnoteDepth == 1);
  reader.restoreSavedPosition();
  assert(reader.currentSpineIndex == 0 && reader.pendingOffsetJump == 1200);
  reader.section = std::make_unique<Section>();
  reader.toolbarJump(1, false);
  reader.toolbarJump(0, false);
  assert(reader.footnoteDepth == 0);
''')

    def test_return_queries_show_the_top_page_or_an_empty_stack(self):
        self.check(r'''
  EpubReaderActivity reader;
  assert(!reader.hasReturnPosition() && reader.returnPageNumber() == -1);
  reader.navigateToHref("c2.xhtml", true);
  assert(reader.hasReturnPosition() && reader.returnPageNumber() == 13);
  reader.restoreSavedPosition();
  assert(!reader.hasReturnPosition() && reader.returnPageNumber() == -1);
''')

    def test_a_jump_marks_its_paint_so_a_starved_one_can_take_its_place_back(self):
        # dropFailedJumpReturn (the reader's paint, tested in test/reader_build_budget) reads this mark.
        self.check(r'''
  EpubReaderActivity reader;
  assert(!reader.returnPushedForLoad);
  reader.navigateToHref("c2.xhtml", true);
  assert(reader.returnPushedForLoad && reader.footnoteDepth == 1);
''')

    def test_chapter_targets_compare_the_page_and_keep_missing_anchors_unchanged(self):
        self.check(r'''
  EpubReaderActivity reader;
  assert(reader.chapterTargetDiffers(1, ""));
  assert(reader.chapterTargetDiffers(0, ""));
  assert(!reader.chapterTargetDiffers(0, "here"));
  assert(reader.chapterTargetDiffers(0, "elsewhere"));
  assert(!reader.chapterTargetDiffers(0, "missing"));
  reader.section->currentPage = 0;
  assert(!reader.chapterTargetDiffers(0, ""));
  reader.section.reset();
  reader.nextPageNumber = 12;
  // No section: the anchor map is not read under the render lock, the jump counts as a move.
  assert(reader.chapterTargetDiffers(0, "here"));
  assert(reader.chapterTargetDiffers(0, "elsewhere"));
''')

    def test_a_wake_pull_that_moved_the_place_leaves_the_old_page_one_back_press_away(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.currentSpineIndex = 1;
  reader.nextPageNumber = 5;
  koautosync::pulledChange = {"book", 0, 12, 1, 5, 1200, true};
  koautosync::hasPulledChange = true;
  reader.pushPulledReturn();
  assert(reader.footnoteDepth == 1 && reader.hasReturnPosition());
  assert(reader.returnPageNumber() == 13);
  assert(!koautosync::progressJustPulled());
  reader.restoreSavedPosition();
  assert(reader.currentSpineIndex == 0 && reader.nextPageNumber == 12 && reader.pendingOffsetJump == 1200);
  assert(!reader.hasReturnPosition());
''')

    def test_a_pull_that_kept_the_place_or_belongs_to_another_book_leaves_the_stack_empty(self):
        self.check(r'''
  EpubReaderActivity reader;
  reader.pushPulledReturn();  // no pull at all
  assert(reader.footnoteDepth == 0);
  reader.currentSpineIndex = 1;
  reader.nextPageNumber = 5;
  koautosync::pulledChange = {"book", 1, 5, 1, 5, 0, false};  // moved nowhere
  koautosync::hasPulledChange = true;
  reader.pushPulledReturn();
  assert(reader.footnoteDepth == 0 && !koautosync::progressJustPulled());
  koautosync::pulledChange = {"other", 0, 12, 1, 5, 0, false};
  koautosync::hasPulledChange = true;
  reader.pushPulledReturn();
  assert(reader.footnoteDepth == 0 && !koautosync::progressJustPulled());  // used up, not kept for a later book
  koautosync::pulledChange = {"book", 0, 12, 2, 7, 0, false};  // the place on disk is not the pulled one
  koautosync::hasPulledChange = true;
  reader.pushPulledReturn();
  assert(reader.footnoteDepth == 0);
''')

    def test_the_pull_return_joins_a_stack_read_from_the_card_and_drops_the_oldest_when_full(self):
        self.check(r'''
  files["cache/links.bin"] = {3, 0, 0, 12, 0, 1, 0, 12, 0, 2, 0, 12, 0};
  EpubReaderActivity reader;
  reader.loadLinkStack();
  assert(reader.footnoteDepth == 3);
  reader.currentSpineIndex = 4;
  reader.nextPageNumber = 8;
  koautosync::pulledChange = {"book", 3, 2, 4, 8, 0, false};
  koautosync::hasPulledChange = true;
  reader.pushPulledReturn();
  assert(reader.footnoteDepth == 3);
  assert(reader.savedPositions[0].spineIndex == 1 && reader.savedPositions[2].spineIndex == 3);
  assert(reader.savedPositions[2].pageNumber == 2 && reader.savedPositions[2].textOffset == UINT32_MAX);
''')

    def test_load_book_pushes_the_pull_return_right_after_it_reads_the_stack(self):
        text = (REPO / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        self.assertRegex(text, r'if \(!preview\) \{\s*loadLinkStack\(\);\s*pushPulledReturn\(\);\s*\}')


if __name__ == '__main__':
    unittest.main(verbosity=2)
