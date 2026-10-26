int failures = 0, tests = 0;
void require(bool yes, const std::string& message) { if (!yes) throw std::runtime_error(message); }
void test(const char* name, const std::function<void()>& run) {
  ++tests;
  try { run(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) { ++failures; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
}
std::string where(const EpubReaderActivity& r) {
  return "spine " + std::to_string(r.savedSpine) + " page " + std::to_string(r.savedPage);
}
// The reader on the last page of the last chapter (7 pages), the chapter still laid out in memory.
void onLastPage(EpubReaderActivity& r) {
  r.currentSpineIndex = 2;
  r.section = r.layoutChapter(2);
  r.section->currentPage = 6;
}
// The press on the last page: the end screen. The section stays, as latTrangThat leaves it.
void reachEnd(EpubReaderActivity& r) {
  require(r.latTrangThat(true) && r.currentSpineIndex == 3, "setup: the turn did not reach the end screen");
}

int main() {
  test("back from the end screen keeps the last page on screen", [] {
    EpubReaderActivity r;
    onLastPage(r);
    reachEnd(r);
    r.onReturnFromEndOfBook();
    r.paint();
    require(r.savedSpine == 2 && r.savedPage == 6, "the last page was not kept: " + where(r));
  });
  test("a bookmark in another chapter after the return lands on the bookmarked page", [] {
    EpubReaderActivity r;
    onLastPage(r);
    reachEnd(r);
    r.onReturnFromEndOfBook();
    r.paint();
    r.currentSpineIndex = 0;  // the jump of EpubReaderActivity::jumpToProgress
    r.nextPageNumber = 3;
    r.section.reset();
    r.paint();
    require(r.savedSpine == 0 && r.savedPage == 3, "the bookmark landed off its page: " + where(r));
  });
  test("a text size change after the side hold keeps the place being read", [] {
    EpubReaderActivity r;
    onLastPage(r);
    reachEnd(r);
    r.onReturnFromEndOfBook();  // SIDE_HOLD_PREV: the end screen goes back, then one more page
    require(r.latTrangThat(false), "setup: the page back was refused");
    r.paint();
    require(r.savedSpine == 2 && r.savedPage == 5, "setup: the hold did not end on the page before the last: " + where(r));
    r.danLaiTrang();
    r.charsPerPage = 80;  // the new size: the same text in more pages
    r.pages[2] = 9;
    r.paint();
    require(r.savedSpine == 2 && r.savedPage == 500 / 80, "the new size lost the place: " + where(r));
  });
  test("back from the end screen of a book reopened there lands on the last page", [] {
    EpubReaderActivity r;
    r.currentSpineIndex = 3;  // restored at the end: no chapter in memory
    r.onReturnFromEndOfBook();
    r.paint();
    require(r.savedSpine == 2 && r.savedPage == 6, "the chapter did not open on its last page: " + where(r));
  });
  std::cout << "RESULT " << tests - failures << "/" << tests << " passed\n";
  return failures ? 1 : 0;
}
