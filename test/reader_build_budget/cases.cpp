int failures = 0;
template<class F> void test(const char* name, F fn) {
  ESP = {}; SETTINGS = {}; pageReads = {}; RenderLock::busy = false;
  freeink::ble::busyState = false;
  freeink::ble::initializingState = false;
  freeink::ble::readerStartDeferredState = false;
  freeink::ble::idleStoppedState = false;
  freeink::ble::stopForIdleCalls = freeink::ble::rearmRequests = 0;
  freeink::ble::stopForIdleResult = true;
  clockMs = 1000; popupCount = buildErrors = blockingPopups = 0; popupAtMs = 0; panelRefreshing = false; thumbs = {}; tenorchrome::enabledState = true;
  activityManager.sleepTransitionState = false; activityManager.deferred.clear(); openWrites = {};
  ImageBlock::hook = nullptr;
  try { fn(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
int main() {
  test("900ms restore with successful first tick stays silent", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->currentPage = r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 30; r.section->startMs = 900;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->ticks == 1 && r.section->pageCount > 30, "first tick did not cross restored watermark");
    require(popupCount == 0, "sub-deadline restore painted indexing popup");
    require(r.pagesUntilFullRefresh == 5, "sub-deadline restore changed refresh cadence");
  });
  // Presses queued behind a paint ran past the laid-out pages. Each page on the way was laid out,
  // then loaded, warmed and drawn only to be dropped before the panel: ~400 ms a page (X3 r03).
  test("a turn queued behind the paint stops it once its page is laid out", [] {
    for (const char* waiting : {static_cast<const char*>(nullptr), "queued"}) {
      EpubReaderActivity r; r.section->building = false;
      r.section->currentPage = r.section->pageCount = r.section->oldPages = 30;
      r.section->restoredPagesAfterStart = 30; r.section->startMs = 900;
      r.nextScreen = waiting;
      { RenderLock held; r.foreground(); }
      require(r.section && r.section->pageCount > 30, "the page the turn stepped onto was not laid out");
      if (!waiting) {
        require(!r.paintDropped && r.repositions == 1, "a paint with nothing queued behind it stopped");
        continue;
      }
      require(r.paintDropped, "a page the queue has left went on to be loaded and drawn");
      require(r.repositions == 0, "the paint went past the layout of a page the queue has left");
      require(r.progressSaveDeferred, "the dropped paint did not leave its progress write to the next one");
    }
  });
  test("15000ms restore with successful first tick stays silent", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->currentPage = r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 30; r.section->startMs = 15000;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->ticks == 1 && r.section->pageCount > 30, "first tick did not cross restored watermark");
    require(popupCount == 0, "restore-only delay painted indexing popup before useful work");
    require(r.pagesUntilFullRefresh == 5, "restore-only delay reset refresh cadence");
  });
  test("slow repeated ticks paint progress once after useful work", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->currentPage = r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 6; r.section->startMs = 900; r.section->tickMs = 400;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->ticks > 1 && r.section->pageCount > 30, "slow extension did not reach target");
    require(popupCount == 1, "slow useful work did not paint exactly one popup");
    require(popupAtMs >= 2300, "popup appeared before a successful slow tick");
    require(r.pagesUntilFullRefresh == 1, "painted progress did not schedule full refresh");
  });
  test("failed first tick after slow restore stays silent", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->currentPage = r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 30; r.section->startMs = 15000; r.section->failTick = true;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(!r.section && buildErrors == 1, "failed first tick did not report build error");
    require(popupCount == 0, "failed first tick painted an extra indexing popup");
    require(r.pagesUntilFullRefresh == 5, "failed first tick reset refresh cadence");
  });
  test("initial resume restore with successful first tick stays silent", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 30; r.section->startMs = 15000;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.initialResume(30); }
    require(r.section && r.section->ticks == 1 && r.section->pageCount > 30, "initial resume first tick missed target");
    require(popupCount == 0, "initial resume restore painted popup before useful work");
    require(r.pagesUntilFullRefresh == 5, "initial resume restore reset refresh cadence");
  });
  test("initial resume repeated ticks paint progress once", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 6; r.section->startMs = 900; r.section->tickMs = 400;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.initialResume(30); }
    require(r.section && r.section->ticks > 1 && r.section->pageCount > 30, "initial resume did not reach target");
    require(popupCount == 1, "initial resume slow work did not paint exactly one popup");
    require(popupAtMs >= 2300, "initial resume popup appeared before a successful tick");
    require(r.pagesUntilFullRefresh == 1, "initial resume popup did not schedule full refresh");
  });
  test("initial resume failed first tick stays silent", [] {
    EpubReaderActivity r; r.section->building = false;
    r.section->pageCount = r.section->oldPages = 30;
    r.section->restoredPagesAfterStart = 30; r.section->startMs = 15000; r.section->failTick = true;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.initialResume(30); }
    require(!r.section && buildErrors == 1, "initial resume failed tick did not report build error");
    require(popupCount == 0, "initial resume failed tick painted an extra popup");
    require(r.pagesUntilFullRefresh == 5, "initial resume failed tick reset refresh cadence");
  });
  test("short watermark extension keeps refresh cadence without popup", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 19;
    r.section->startMs = 50; r.section->tickMs = 80; r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->pageCount > 19, "short extension missed target");
    require(popupCount == 0, "short extension painted indexing popup");
    require(r.pagesUntilFullRefresh == 5, "short extension changed refresh cadence");
    require(!r.buildPopupPending, "short extension left popup pending");
  });
  // X3 r43: a contents jump laid out 93 pages in 4.4 s and stood 390 ms of it on the popup's
  // refresh. The layout goes on while the panel shows the popup, and the page waits for it.
  test("a slow build lays out pages while its popup refreshes", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 30;
    r.section->tickMs = 400; r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->pageCount > 30, "slow extension missed target");
    require(popupCount == 1, "slow extension did not show its popup");
    require(blockingPopups == 0 && r.section->ticksWhileRefreshing > 0,
            "the layout waited out the popup's refresh before its next step");
    require(!panelRefreshing, "the build handed the page a refresh still running");
    require(r.pagesUntilFullRefresh == 1, "the popup did not schedule a full refresh");
  });
  test("slow watermark extension paints once after deadline", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 30;
    r.section->tickMs = 400; r.pagesUntilFullRefresh = 5;
    const uint32_t startedAt = millis();
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->pageCount > 30, "slow extension missed target");
    require(popupCount == 1, "slow extension did not paint exactly one popup");
    require(popupAtMs - startedAt >= r.BUILD_POPUP_DEADLINE_MS, "popup appeared before deadline");
    require(r.pagesUntilFullRefresh == 1, "painted popup did not schedule full refresh");
    require(!r.buildPopupPending, "slow extension left popup pending");
  });
  test("cached page leaves popup and refresh cadence alone", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 18;
    r.pagesUntilFullRefresh = 5;
    { RenderLock held; r.foreground(); }
    require(popupCount == 0 && r.pagesUntilFullRefresh == 5, "cached page changed popup or refresh state");
    require(r.section && r.section->starts == 0, "cached page restarted parser");
  });
  test("watermark start failure clears popup pending and reports error", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 19;
    r.section->failStart = true; r.buildPopupPending = true;
    { RenderLock held; r.foreground(); }
    require(!r.section && buildErrors == 1, "start failure did not terminate with build error");
    require(!r.buildPopupPending, "start failure left popup pending");
  });
  test("watermark tick failure clears popup pending and reports error", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 19;
    r.section->failTick = true; r.buildPopupPending = true;
    { RenderLock held; r.foreground(); }
    require(!r.section && buildErrors == 1, "tick failure did not terminate with build error");
    require(!r.buildPopupPending, "tick failure left popup pending");
  });
  test("transient page read retries cached target without rebuilding", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 18; r.nextPageNumber = 0; pageReads.failures = 1;
    const auto* original = r.section.get();
    { RenderLock held; r.loadPageForRender(); }
    require(r.section.get() == original && r.section->currentPage == 18, "transient read discarded target section");
    require(pageReads.clears == 0 && pageReads.abandons == 0, "transient read destroyed valid cache");
    require(r.requests == 1 && r.pageLoadRetryCount == 1, "first failure did not queue one bounded retry");
    { RenderLock held; r.loadPageForRender(); }
    require(pageReads.reads == 2 && r.pageLoadRetryCount == 0, "cached retry did not recover");
    require(pageReads.clears == 0 && pageReads.errors == 0, "recovered read rebuilt or reported an error");
  });
  test("persistent page read rebuilds once and preserves requested page", [] {
    EpubReaderActivity r; r.section->currentPage = 18; r.nextPageNumber = 0; pageReads.failures = 2;
    { RenderLock held; r.loadPageForRender(); }
    if (!r.section) { r.section = std::make_unique<Section>(); r.section->currentPage = r.nextPageNumber; }
    { RenderLock held; r.loadPageForRender(); }
    require(pageReads.clears == 1 && pageReads.abandons == 1, "persistent read did not bound cache invalidation to one rebuild");
    require(!r.section && r.nextPageNumber == 18, "rebuild lost requested page to stale resume position");
    require(r.requests == 2, "rebuild was not queued once");
  });
  test("terminal page read error stops with one rebuild and retains retry target", [] {
    EpubReaderActivity r; r.section->currentPage = 18; pageReads.failures = 10;
    for (int n = 0; n <= r.MAX_PAGE_LOAD_RETRIES; ++n) {
      if (!r.section) { r.section = std::make_unique<Section>(); r.section->currentPage = r.nextPageNumber; }
      RenderLock held; r.loadPageForRender();
    }
    require(pageReads.clears == 1 && pageReads.abandons == 1, "terminal failure repeatedly regenerated cache");
    require(r.requests == r.MAX_PAGE_LOAD_RETRIES && pageReads.errors == 1, "retry did not terminate at existing limit");
    require(r.section && r.section->currentPage == 18 && r.pageLoadRetryCount == 0, "terminal error lost reading target");
  });
  test("transient active-build read keeps completed pages and parser", [] {
    EpubReaderActivity r; pageReads.failures = 1;
    { RenderLock held; r.loadPageForRender(); }
    require(r.section && r.section->isBuilding() && r.section->pageCount == 19, "transient read discarded in-progress index");
    require(pageReads.abandons == 0 && pageReads.clears == 0, "transient read destroyed active build");
  });
  test("background tick failure preserves retry target and one no-section request", [] {
    EpubReaderActivity r; r.section->currentPage = 18; r.nextPageNumber = 0;
    r.section->failTick = true;
    r.backgroundTick();
    require(!r.section, "failed background tick retained invalid build");
    require(r.nextPageNumber == 18, "failed background tick lost current page to stale retry target");
    require(r.requests == 1, "failed background tick did not request exactly one retry");
    for (int n = 0; n < 20; ++n) r.backgroundTick();
    require(r.requests == 1, "idle retry loop emitted unbounded update requests");
  });
  test("background failure latches speculative rebuild across reload", [] {
    EpubReaderActivity r; r.section->currentPage = 18; r.nextPageNumber = 0;
    r.section->failTick = true;
    r.backgroundTick();
    require(!r.section && r.nextPageNumber == 18 && r.requests == 1, "initial failure did not preserve reload target");

    r.section = std::make_unique<Section>();
    r.section->building = false; r.section->currentPage = r.nextPageNumber;
    r.section->pageCount = r.section->oldPages = 19; r.buildViewportWidth = 515;
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section && r.section->currentPage == 18, "latched reload changed preserved target");
    require(r.section->starts == 0 && r.section->ticks == 0, "latched reload restarted speculative build");
    require(r.requests == 1, "latched reload emitted extra update requests");

    r.section->currentPage = 19;
    { RenderLock held; r.foreground(); }
    require(r.section && r.section->currentPage == 19 && r.section->pageCount > 19,
            "explicit foreground target did not extend latched partial cache");
    require(!r.section->isBuilding(), "latched foreground extension retained parser before render");
    require(r.requests == 1, "explicit foreground extension changed retry request count");
  });
  test("fresh reader restores background admission after prior visit failure", [] {
    {
      EpubReaderActivity failedVisit; failedVisit.section->currentPage = 18;
      failedVisit.section->failTick = true;
      failedVisit.backgroundTick();
      require(!failedVisit.section && failedVisit.requests == 1, "prior visit did not enter failed state");
    }
    EpubReaderActivity freshVisit; freshVisit.section->building = false;
    freshVisit.section->currentPage = 18; freshVisit.buildViewportWidth = 515;
    freshVisit.backgroundTick();
    require(freshVisit.section->starts == 1 && freshVisit.section->ticks == 1,
            "fresh reader inherited prior visit background latch");
  });
  test("low heap releases active build and keeps larger partial/current page", [] {
    EpubReaderActivity r; ESP.free = 29100; ESP.largest = 17396;
    r.backgroundTick();
    require(!r.section->isBuilding(), "parser/CSS/LUT retained after gate fails");
    require(r.section->pageCount == 19 && r.section->currentPage == 4, "lost readable partial or position");
    require(r.section->suspends == 1, "expected exactly one suspension");
    require(!r.skipLoopDelay(), "paused build busy-spins");
  });
  test("low heap parks parser then recovers after headroom returns", [] {
    EpubReaderActivity r; r.section->canPark = true; ESP.free = 29100; ESP.largest = 17396;
    r.backgroundTick();
    require(r.section->isBuilding() && r.section->isBuildParked(), "low heap did not park active parser");
    require(r.section->parks == 1 && r.section->suspends == 0, "parking used partial-commit fallback");
    require(r.section->pageCount == 19 && r.section->currentPage == 4, "parking lost readable pages or position");
    require(!r.skipLoopDelay(), "parked heap latch busy-spins");
    // Recovered means recovered past the parser this park released (80000 - 29100) plus the
    // tick budget it must stay above, not just past the start gate.
    ESP.free = 96000; ESP.largest = 60000;
    r.backgroundTick();
    require(r.section->ticks == 1 && r.section->resumes == 1 && !r.section->isBuildParked(),
            "recovered heap did not resume the parked parser");
  });
  test("starved extension without radio keeps built pages and warns", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = false; r.section->starveUntilRadioStopped = true;
    r.section->currentPage = r.section->pageCount;
    { RenderLock held; r.foreground(); }
    require(freeink::ble::stopForIdleCalls == 0, "radio touched while disabled");
    require(buildErrors == 0 && r.section, "starved build reported as index failure");
    require(r.section->suspends == 1 && r.section->currentPage == r.section->pageCount - 1,
            "starved build lost built pages or reading position");
    require(popupCount == 1, "memory warning not shown");
  });
  test("largest-block failure also releases outside build-ahead window", [] {
    EpubReaderActivity r; r.section->partial = false; ESP.largest = 16000;
    r.backgroundTick(); require(!r.section->isBuilding(), "retained build outside window");
  });
  test("heap crossing within tick releases before next frame", [] {
    EpubReaderActivity r; r.section->dropAfterTick = true;
    r.backgroundTick(); require(r.section->ticks == 1, "tick never ran");
    require(!r.section->isBuilding(), "tick retained low-memory parser");
  });
  test("memory recovery after suspension cannot restart background loop", [] {
    EpubReaderActivity r; r.buildViewportWidth = 515; ESP.free = 29000;
    r.backgroundTick();
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->starts == 0 && r.section->suspends == 1, "repeated start/suspend after release");
  });
  test("preflight stops parser allocation at measured BLE-live envelope", [] {
    EpubReaderActivity r; r.section->building = false; r.buildViewportWidth = 515;
    ESP.free = 53428; ESP.largest = 42996;
    r.backgroundTick(); require(r.section->starts == 0, "allocated parser using tick budget");
  });
  test("healthy preflight still extends partial", [] {
    EpubReaderActivity r; r.section->building = false; r.buildViewportWidth = 515;
    r.backgroundTick(); require(r.section->starts == 1 && r.section->ticks == 1, "healthy extension blocked");
  });
  test("failed try-lock keeps build until next safe iteration", [] {
    EpubReaderActivity r; ESP.free = 29000;
    { RenderLock held; r.backgroundTick(); require(r.section->suspends == 0, "mutated render-owned build"); }
    r.backgroundTick(); require(r.section->suspends == 1, "did not retry safe release");
  });
  test("suspended watermark Next remains in chapter and explicit target builds", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 18;
    { RenderLock held; require(r.latTrangThat(true), "Next refused"); }
    require(r.currentSpineIndex == 0 && r.section && r.section->currentPage == 19, "partial watermark treated as chapter end");
    { RenderLock held; r.foreground(); }
    require(r.section->pageCount > 19 && r.section->currentPage == 19, "explicit target did not become readable");
  });
  test("foreground target releases low-memory build before page render", [] {
    EpubReaderActivity r; r.section->building = false; r.section->currentPage = 19;
    r.section->dropAfterTick = true;
    { RenderLock held; r.foreground(); }
    require(!r.section->isBuilding(), "foreground retained parser before AA");
    require(r.section->pageCount > 19 && r.section->currentPage == 19, "foreground lost requested page");
  });
  test("start allocation crossing budget releases immediately", [] {
    EpubReaderActivity r; r.section->building = false; r.buildViewportWidth = 515;
    r.section->dropAfterStart = true;
    r.backgroundTick(); require(r.section->starts == 1 && r.section->suspends == 1, "start retained parser below tick budget");
  });
  test("accepted queued external turn survives suspension and drains once", [] {
    EpubReaderActivity r; r.section->currentPage = 18; ESP.free = 29000;
    require(r.luotLatTrangNgoai(true), "external turn rejected");
    { RenderLock held; r.backgroundTick(); r.processExternalPageTurn(); }
    require(r.pendingExternalTurn == 1 && r.section->currentPage == 18, "busy render lost or executed queued turn");
    r.backgroundTick();
    require(!r.section->isBuilding(), "build still resident before queued turn");
    r.processExternalPageTurn();
    require(r.section && r.currentSpineIndex == 0 && r.section->currentPage == 19, "queued Next skipped partial chapter");
    require(r.requests == 1 && r.trangDaLat == 1 && r.pendingExternalTurn == 0, "queued turn did not drain exactly once");
    r.backgroundTick(); r.processExternalPageTurn();
    require(r.requests == 1 && r.trangDaLat == 1, "queued turn replayed");
  });
  // Heap values below are the ones the device logged right after the first page of a cold open
  // painted: PAINT_COMPLETE rid=1 heap=116768 largest=90100 with the page-turner radio off, and
  // MEM Free: 61204 bytes, MaxAlloc: 55284 bytes once the radio started (cand-newbook-open.log).
  test("cover thumbnail leaves the reading passes and is written as the reader closes", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    // One look per height: the card's own and the theme's.
    require(thumbs.existsChecks == 2, "open path stopped looking for the cover thumbnails");
    require(thumbs.generated == 0, "open path still paid for the cover thumbnail");
    // A cover page drawn before the exit writes them from its own decode.
    require(ImageBlock::hook && ImageBlock::hook == r.coverThumbs.get(),
            "the cover page decode was not asked for them");
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    for (int pass = 0; pass < 3; ++pass) {
      clockMs += 500;  // past IDLE_PREWARM_DEBOUNCE_MS, the page is on the panel
      r.idleStep();
    }
    // Each decode held the buttons for about 3 s on the X3 under the page being read.
    require(thumbs.generated == 0, "idle pass decoded the cover under the page being read");
    r.writePendingThumbs();
    // The card's own height first: it is the one the card draws.
    require((thumbs.heights == std::vector<int>{356, 226}), "closing the reader did not write both thumbnails");
    // The copy's 32 KB inflate window comes out of the framebuffer, which the next screen redraws
    // whole: X3 r19 lost both thumbnails when the heap's largest block fell 12 bytes short.
    require(thumbs.loans == 1, "closing thumbnail did not borrow the framebuffer");
    r.writePendingThumbs();
    require(thumbs.generated == 2, "a second close wrote the thumbnails again");
  });
  test("radio heap still gets the new book its cover when the reader closes", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    ESP.free = 61204; ESP.largest = 55284;
    r.lastRenderCompleteMs = millis();
    for (int pass = 0; pass < 3; ++pass) {
      clockMs += 500;
      r.idleStep();
    }
    r.writePendingThumbs();
    require((thumbs.heights == std::vector<int>{356, 226}),
            "a book read with the radio on reached Home without a cover");
  });
  test("book opened before the card thumbnail gets it when the reader closes", [] {
    EpubReaderActivity r; thumbs.onCard = {226};
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    r.writePendingThumbs();
    require(thumbs.heights == std::vector<int>{356}, "only the missing card thumbnail should be generated");
  });
  test("other themes keep the theme thumbnail alone", [] {
    EpubReaderActivity r; tenorchrome::enabledState = false;
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    require(thumbs.existsChecks == 1, "a theme without the card looked for the card thumbnail");
    r.writePendingThumbs();
    require(thumbs.heights == std::vector<int>{226}, "a theme without the card paid for the card thumbnail");
  });
  test("sleep does not wait on a cover decode", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    activityManager.sleepTransitionState = true;
    r.writePendingThumbs();
    require(thumbs.generated == 0, "the power key waited on a cover decode");
    require(popupCount == 0, "a notice for a decode that did not run");
  });
  // The decode as the reader closes held the page still for 1 to 3 s on the X3 with nothing on
  // the panel to say so (review V4). A short notice goes up first.
  test("closing the reader puts up a notice before it decodes the cover", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    r.writePendingThumbs();
    require(popupCount == 1 && popupGenerated == 0, "the page stood still without a notice while the cover decoded");
    require(thumbs.generated == 2, "the notice replaced the decode");
    r.writePendingThumbs();
    require(popupCount == 1, "a notice with nothing to decode");
  });
  test("existing cover thumbnail asks for nothing on either path", [] {
    EpubReaderActivity r; thumbs.fileOnCard = true;
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    require(!ImageBlock::hook, "a book with its thumbnails still hooks the cover decode");
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    clockMs += 500;
    r.idleStep();
    r.writePendingThumbs();
    require(thumbs.generated == 0, "warm open regenerated an existing thumbnail");
    require(thumbs.loans == 0, "warm open borrowed the framebuffer for nothing");
  });
  // Pausing for the reader menu wrote the reading-stats checkpoint first: 245 ms on the X3 before
  // the menu could paint (r18-k2: PAUSE_SAVE ms=245). The menu keeps the record in RAM; the reader
  // writes it on its next 30 s checkpoint after the menu closes, or on its exit (sleep included).
  test("the reader menu opens without writing the stats; the next checkpoint writes them", [] {
    EpubReaderActivity r; r.statsEnabled = true; r.statsActive = true; r.pageReady = true;
    r.statsLastMs = r.statsSavedMs = r.statsDayPollMs = millis();
    clockMs += 20000;  // twenty seconds of reading before the menu
    r.pauseKeepsStatsInRam = true;
    r.onPause();
    require(openWrites.statsSaves == 0, "the menu waited on a stats write");
    require(r.statsDirty, "the reading before the menu was not recorded");
    clockMs += 12000;  // menu time, the reader does not tick
    r.onTick();       // first tick back on the page: past the 30 s checkpoint
    require(openWrites.statsSaves == 1, "the checkpoint after the menu did not write the stats");
    clockMs += 5000;
    r.onPause();  // a later screen that is not the menu
    activityManager.nextScreenFramed();
    require(openWrites.statsSaves == 2, "the menu flag leaked into a later pause");
    require(kMenuKeepsStats, "openReaderMenu does not ask the pause to keep the stats in RAM");
  });
  // Text settings open from the reader menu (or the reader's text panel): the pause as they opened
  // wrote the checkpoint, 274 ms on the X3 before the screen could paint (sweep-int6: PAUSE_SAVE
  // ms=274). They are the menu's own screens and keep the record in RAM the same way.
  test("text settings open without writing the stats", [] {
    EpubReaderActivity r; r.statsEnabled = true; r.statsActive = true; r.pageReady = true;
    r.statsLastMs = r.statsSavedMs = r.statsDayPollMs = millis();
    clockMs += 20000;
    r.pauseKeepsStatsInRam = kTextSettingsKeepsStats;
    r.onPause();
    require(openWrites.statsSaves == 0, "text settings waited on a stats write");
    require(r.statsDirty, "the reading before text settings was not recorded");
  });
  // Any other screen over the reader wrote the checkpoint before its first frame: 367 ms on the X3
  // in front of the quote selector (r12: PAUSE_SAVE ms=367). It is written once that screen's first
  // frame is up, or as the screen closes (ActivityManager flushes deferred writes on every exit).
  test("other screens over the reader write the stats after their first frame", [] {
    EpubReaderActivity r; r.statsEnabled = true; r.statsActive = true; r.pageReady = true;
    r.statsLastMs = r.statsSavedMs = r.statsDayPollMs = millis();
    clockMs += 20000;
    r.onPause();
    require(openWrites.statsSaves == 0, "a child screen waited on a stats write before its first frame");
    require(activityManager.deferred.size() == 1, "the reading before the child screen was left unwritten");
    activityManager.nextScreenFramed();
    require(openWrites.statsSaves == 1, "the child screen's first frame did not bring the stats write");
    clockMs += 1000;
    r.onPause();  // nothing read since: nothing to write
    require(activityManager.deferred.empty(), "an unchanged record was written again");
  });
  // state.json and the recent list are read by the next boot and by Home only. Writing them in
  // onEnter() put two SD writes (and a pass over every recent book on the card) ahead of the
  // first page.
  test("opening a book records it after the first frame, not before", [] {
    EpubReaderActivity r;
    r.openTail();
    require(openWrites.stateSaves == 0 && openWrites.recentAdds == 0,
            "the open wrote state and recents ahead of the first frame");
    r.onTick();
    require(openWrites.recentAdds == 0, "the open was recorded before a frame reached the panel");
    r.pageReady = true;
    r.pageRendered = true;
    r.onTick();
    require(openWrites.stateSaves == 1 && openWrites.recentAdds == 1, "the first frame did not record the open");
    r.onTick();
    require(openWrites.stateSaves == 1 && openWrites.recentAdds == 1, "the open was recorded twice");
  });
  test("a book opened on its end screen is recorded once that screen is drawn", [] {
    EpubReaderActivity r;
    r.openTail();
    r.endOfBookOptionsReady = true;
    r.pageRendered = true;  // ReaderActivity::render marks the end screen as a rendered page
    r.onTick();
    require(openWrites.recentAdds == 1, "the end-of-book screen did not record the open");
  });
  // A book that cannot be laid out shows an error screen: that frame must not make it the book
  // the next wake reopens, nor put it on the recent list.
  test("an open whose first frame is an error screen is not recorded", [] {
    EpubReaderActivity r;
    r.openTail();
    r.pageReady = true;
    r.onTick();
    r.onTick();
    require(openWrites.stateSaves == 0 && openWrites.recentAdds == 0, "an error screen recorded the open");
    r.commitOpen();
    require(openWrites.stateSaves == 0 && openWrites.recentAdds == 0, "closing after an error screen recorded the open");
  });
  // Device evidence (serial-r14-rx.log): EPUB_PARK free=50564 then EPUB_RESUME free=28924,
  // so the parser takes back about 21 KB and lands under the 32 KB tick budget.
  test("parked parser is not resumed into an immediate re-park", [] {
    EpubReaderActivity r; r.section->canPark = true;
    r.section->parkRestoresFree = 66000; r.section->parkRestoresLargest = 60000;
    r.section->parserFootprint = 35000;
    ESP.free = 29100; ESP.largest = 17396;
    r.backgroundTick();
    require(r.section->isBuildParked() && r.section->parks == 1, "heap pressure did not park the parser");
    for (int n = 0; n < 20; ++n) r.backgroundTick();
    require(r.section->resumes == 0, "parked parser was resumed only to be parked again");
    require(r.section->parks == 1, "park and resume churn continued after the first park");
    require(!r.skipLoopDelay(), "refused resume busy-spins");
    ESP.free = 116768; ESP.largest = 90100;
    r.backgroundTick();
    require(r.section->resumes == 1 && !r.section->isBuildParked(),
            "heap that covers the parser did not resume the parked build");
  });
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  // X3 r43 (v1.0.16): the radio's stack came up beside a live layout parser and left a largest
  // block of 32,756 B, under the 32,768 B its own check keeps, so the start was rolled back; the
  // parser was parked 640 ms later. The reader parks it before the radio is started.
  test("the layout parser is parked before the radio starts", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = false;
    freeink::ble::initializingState = false;
    freeink::ble::readerStartDeferredState = false;
    require(r.readyForRadio(), "reader kept the radio waiting with its render lock free");
    require(r.section->isBuildParked() && r.section->parks == 1 && r.section->suspends == 0,
            "the radio was let start beside a live layout parser");
    require(r.readyForRadio() && r.section->parks == 1, "a parked parser was parked again");
    EpubReaderActivity painting; painting.section->canPark = true;
    {
      RenderLock paint;
      require(!painting.readyForRadio(), "the radio was let start while the page painted");
    }
    require(painting.section->parks == 0 && !painting.section->isBuildParked(), "a painting reader was parked");
    EpubReaderActivity laidOut; laidOut.section->building = false;
    require(laidOut.readyForRadio() && laidOut.section->parks == 0, "a chapter laid out whole held the radio");
  });
  test("BLE idle with enabled setting admits background parser", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.buildViewportWidth = 515;
    freeink::ble::busyState = false;
    freeink::ble::initializingState = false;
    freeink::ble::readerStartDeferredState = false;
    r.backgroundTick();
    require(r.section->ticks == 1 && r.section->isBuilding(), "idle BLE setting parked background parser");
  });
  test("BLE busy park stays idle until radio becomes idle", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true;
    r.buildViewportWidth = 515;
    r.backgroundTick();
    require(r.section->isBuilding() && r.section->isBuildParked(), "BLE policy did not park active parser");
    require(r.section->parks == 1 && r.section->suspends == 0, "BLE parking used partial-commit fallback");
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->ticks == 0 && r.section->resumes == 0 && !r.skipLoopDelay(),
            "BLE-blocked parked parser ticked or busy-spun");
    freeink::ble::busyState = false;
    r.backgroundTick();
    require(r.section->ticks == 1 && r.section->resumes == 1 && !r.section->isBuildParked(),
            "idle BLE did not restore admitted background progress");
  });
  // Device evidence (x3-do-fix2, d3): with the radio up the parser stays parked, the reader
  // sits on the last laid-out page, and every turn paid 140 to 400 ms resuming the parser
  // inside the paint before its page could load. A quiet pass lays out the next page first.
  test("BLE busy lays out the next page on a quiet pass at the frontier", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.backgroundTick();
    require(r.section->isBuildParked() && r.section->parks == 1, "precondition: BLE policy parks the parser");
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 19;
    r.section->currentPage = 18;
    ESP.free = 46124; ESP.largest = 15348;  // BLE-live heap on the X3 between turns
    r.lastRenderCompleteMs = clockMs;
    r.backgroundTick();
    require(r.section->resumes == 0, "look-ahead ran while the page was still settling");
    clockMs += 500;
    r.backgroundTick();
    require(r.section->resumes == 1 && r.section->pageCount > 19, "next page was not laid out on a quiet pass");
    require(r.section->isBuildParked() && r.section->parks == 2, "parser was not handed back after the look-ahead");
    require(freeink::ble::stopForIdleCalls == 0, "look-ahead touched the radio");
    for (int n = 0; n < 10; ++n) r.backgroundTick();
    require(r.section->resumes == 1, "look-ahead repeated for the same page");
  });
  test("look-ahead leaves a radio that is still starting alone", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.backgroundTick();
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 19;
    r.section->currentPage = 18;
    freeink::ble::initializingState = true;
    r.lastRenderCompleteMs = clockMs;
    clockMs += 500;
    r.backgroundTick();
    require(r.section->resumes == 0, "parser resumed while the radio was allocating its start");
  });
  test("look-ahead waits while turns are queued behind the paint", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.backgroundTick();
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 19;
    r.section->currentPage = 18;
    r.pendingManualTurn = 1;
    r.lastRenderCompleteMs = clockMs;
    clockMs += 500;
    r.backgroundTick();
    require(r.section->resumes == 0, "look-ahead ran ahead of a queued turn");
  });
  // Device evidence (sweep-int6, 24/09): a book reopened on the last page of its partial cache.
  // The radio starts right after the first paint, so the extension never started, and the first
  // turn laid the next page out inside its paint: 1.52 s against 0.66 s for the turns after it.
  test("BLE busy starts a partial's extension ahead of the first turn", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.section->building = false;
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 3;
    r.section->restoredPagesAfterStart = 3;
    r.section->currentPage = 2;
    ESP.free = 59072; ESP.largest = 53236;  // BLE-live heap on the X3 at that turn
    r.lastRenderCompleteMs = clockMs;
    r.backgroundTick();
    require(r.section->starts == 0, "extension started while the page was still settling");
    clockMs += 500;
    r.backgroundTick();
    require(r.section->starts == 1 && r.section->pageCount > 3, "next page was not laid out ahead of the first turn");
    require(r.section->isBuildParked(), "parser was not handed back after the look-ahead");
    for (int n = 0; n < 10; ++n) r.backgroundTick();
    require(r.section->starts == 1 && r.section->resumes == 0, "look-ahead repeated for the same page");
  });
  // Device evidence (r27-ui, 25/09): the look-ahead above started the extension but ran one
  // tick, about 20 ms of parsing, then handed the parser back: no page was added and the first
  // turn still laid its page out inside the paint (PAINT_LOAD_BEGIN kind=building, 783 ms).
  test("look-ahead lays out the whole next page, not one parser tick", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.section->building = false;
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 4;
    r.section->restoredPagesAfterStart = 4;
    r.section->currentPage = 3;
    r.section->ticksPerPage = 39;  // 780 ms at 20 ms a tick
    ESP.free = 52136; ESP.largest = 26612;  // after the radio start, r27-ui
    r.lastRenderCompleteMs = clockMs;
    clockMs += 500;
    r.backgroundTick();
    require(r.section->pageCount > r.section->currentPage + 1, "the page after the current one is not laid out");
    require(r.section->isBuildParked(), "parser was not handed back after the look-ahead");
    const int ticks = r.section->ticks;
    for (int n = 0; n < 10; ++n) r.backgroundTick();
    require(r.section->ticks == ticks, "look-ahead kept laying out past the next page");
  });
  // Device evidence (r29, 26/09): the book opened on page 0 of a two-page partial. A quick burst
  // of two turns went past page 1 onto page 2, which nothing had laid out: the paint laid it out
  // (432 ms) and the burst took 1.2 s to be readable. The look-ahead keeps two pages ready.
  test("look-ahead keeps two pages laid out past the one on screen", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.section->building = false;
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 2;
    r.section->restoredPagesAfterStart = 2;
    r.section->currentPage = 0;
    r.section->ticksPerPage = 8;  // 160 ms a page at 20 ms a tick, as r29's LOOK_AHEAD lines
    ESP.free = 53144; ESP.largest = 31732;  // BLE-live heap on the X3 in r29
    r.lastRenderCompleteMs = clockMs;
    clockMs += 500;
    r.backgroundTick();
    require(r.section->pageCount > 2, "the second page past the one on screen is not laid out");
    require(r.section->isBuildParked(), "parser was not handed back after the look-ahead");
    const int ticks = r.section->ticks;
    for (int n = 0; n < 10; ++n) r.backgroundTick();
    require(r.section->pageCount == 3 && r.section->ticks == ticks, "look-ahead kept laying out past two pages");
    // The next turn lays out one page more, not two.
    r.section->currentPage = 1;
    r.lastRenderCompleteMs = clockMs;
    clockMs += 500;
    r.backgroundTick();
    require(r.section->pageCount == 4, "a turn inside the look-ahead did not keep two pages ready");
  });
  test("look-ahead window opens when the radio's start ends, not at the paint", [] {
    EpubReaderActivity r; r.section->canPark = true; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true; r.buildViewportWidth = 515;
    r.section->building = false;
    r.section->builtPages = r.section->oldPages = r.section->pageCount = 3;
    r.section->restoredPagesAfterStart = 3;
    r.section->currentPage = 2;
    r.lastRenderCompleteMs = clockMs;
    freeink::ble::initializingState = true;
    for (int n = 0; n < 6; ++n) {
      clockMs += 300;
      r.backgroundTick();
    }
    require(r.section->starts == 0, "look-ahead ran while the radio was allocating its start");
    freeink::ble::initializingState = false;
    r.backgroundTick();
    require(r.section->starts == 0, "look-ahead ran the moment the radio came up");
    clockMs += 500;
    r.backgroundTick();
    require(r.section->starts == 1 && r.section->pageCount > 3, "window closed while the radio was starting");
  });
  test("starved extension never stops a radio that is still starting", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.section->canPark = true;
    r.section->starveUntilRadioStopped = true; r.section->currentPage = r.section->pageCount;
    freeink::ble::initializingState = true;
    { RenderLock held; r.foreground(); }
    require(freeink::ble::stopForIdleCalls == 0, "radio torn down while its start was in flight");
    require(buildErrors == 0 && r.section && popupCount == 1, "starved build with radio starting did not fall back to the memory notice");
    require(!r.radioReleasedForBuild, "release flagged although the radio was left alone");
  });
  test("starved extension stops radio then finishes the page", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.section->canPark = true;
    r.section->starveUntilRadioStopped = true; r.section->currentPage = r.section->pageCount;
    { RenderLock held; r.foreground(); }
    require(freeink::ble::stopForIdleCalls == 1, "radio not released for a starved build");
    require(buildErrors == 0 && r.section, "starved build reported as index failure");
    require(r.section->pageCount > r.section->currentPage, "starved build did not finish after radio release");
    require(r.radioReleasedForBuild, "radio release not remembered for rearm");
  });
  test("owed progress write waits for a section instead of being forgotten", [] {
    EpubReaderActivity r; r.section.reset();
    r.progressSaveDeferred = true;
    { RenderLock held; r.saveProgressIfMoved(); }
    require(r.progressSaveDeferred, "a chapter jump left no section and the owed write was dropped");
  });
  test("owed progress write that fails stays owed for the exit", [] {
    EpubReaderActivity r; r.progressSaveDeferred = true; r.saveWorks = false;
    { RenderLock held; r.saveProgressIfMoved(); }
    require(r.saveAttempts == 1, "the owed write was not tried");
    require(r.progressSaveDeferred, "a card error dropped the owed write");
    r.saveWorks = true;
    { RenderLock held; r.saveProgressIfMoved(); }
    require(r.saveAttempts == 2 && !r.progressSaveDeferred, "the retry did not settle the owed write");
  });
  test("jump whose anchor is missing from the map stays on the saved page", [] {
    // A one-file book whose anchor map outgrew the heap: the chapter's anchor is not in it.
    EpubReaderActivity r; r.lastSavedSpineIndex = 1; r.lastSavedPage = 7; r.lastSavedPageCount = 19;
    r.currentSpineIndex = 2; r.pendingAnchor = "chuong-280"; r.section->currentPage = 0;
    const int before = r.requests;
    { RenderLock held; r.anchorLanding(); }
    require(r.pendingAnchor.empty(), "the missing anchor stays pending");
    require(r.currentSpineIndex == 1 && r.nextPageNumber == 7, "the reader left the page it was on for the chapter's first page");
    require(!r.section, "the chapter the jump could not place is still the one painted and saved");
    require(r.requests == before + 1, "no repaint of the page the reader stays on");
  });
  test("jump within the chapter whose anchor is missing keeps the page", [] {
    EpubReaderActivity r; r.lastSavedSpineIndex = 2; r.lastSavedPage = 7; r.lastSavedPageCount = 19;
    r.currentSpineIndex = 2; r.pendingAnchor = "doan-12"; r.section->currentPage = 0;
    { RenderLock held; r.anchorLanding(); }
    require(r.section && r.section->currentPage == 7, "the reader left its page for the chapter's first page");
  });
  test("jump to a one-chapter file whose anchor is missing lands on its first page", [] {
    // The file starts with its only chapter, so its first page is the right place, as before.
    EpubReaderActivity r; r.lastSavedSpineIndex = 2; r.lastSavedPage = 7; r.lastSavedPageCount = 19;
    r.currentSpineIndex = 1; r.pendingAnchor = "chuong-2"; r.section->currentPage = 0;
    { RenderLock held; r.anchorLanding(); }
    require(r.section && r.currentSpineIndex == 1 && r.section->currentPage == 0,
            "a one-chapter file was left for the saved page");
  });
  test("jump whose anchor is found lands on it", [] {
    EpubReaderActivity r; r.lastSavedSpineIndex = 1; r.lastSavedPage = 7;
    r.currentSpineIndex = 2; r.pendingAnchor = "chuong-3"; r.section->anchorPage = 12;
    { RenderLock held; r.anchorLanding(); }
    require(r.section && r.section->currentPage == 12 && r.currentSpineIndex == 2, "a found anchor did not land");
  });
  // A jump (percent, contents, anchor, held chapter) or an open whose target chapter the heap could
  // not lay out. The section was created for that chapter this paint, so its current page is 0:
  // painted and saved, the reader lost their place (review V-A). It goes back to the saved page, or
  // with nothing saved yet (an open) keeps its target for the next try; either way nothing is saved.
  const auto starvedTarget = [](EpubReaderActivity& r) {
    r.section->building = false; r.section->partial = false;
    r.section->oldPages = r.section->pageCount = r.section->builtPages = 0; r.section->currentPage = 0;
    r.section->starveUntilRadioStopped = true; r.section->canPark = true;
  };
  // What the next paint would show and whether it writes the progress. A paint loads a missing
  // section at nextPageNumber; with one, it shows its current page. Then it saves if the page moved.
  const auto nextPaint = [](EpubReaderActivity& r) {
    struct Landing { int spine, page; } landing{r.currentSpineIndex, r.section ? r.section->currentPage : r.nextPageNumber};
    { RenderLock held; r.saveProgressIfMoved(); }
    return landing;
  };
  test("percent jump that runs out of heap goes back to the saved page and saves nothing", [starvedTarget, nextPaint] {
    EpubReaderActivity r; starvedTarget(r);
    r.lastSavedSpineIndex = 1; r.lastSavedPage = 7; r.lastSavedPageCount = 19;
    r.currentSpineIndex = 2; r.pendingPercentJump = true; r.pendingSpineProgress = 0.6f;
    { RenderLock held; r.percentJump(); }
    require(popupCount == 1 && buildErrors == 0, "starved percent build did not show the memory notice");
    require(!r.pendingPercentJump, "the percent target stays pending and lands on the next chapter loaded");
    const auto landing = nextPaint(r);
    require(landing.spine == 1 && landing.page == 7, "the next paint shows page 0 of the target chapter");
    require(r.saveAttempts == 0, "page 0 of the target chapter was saved as the progress");
  });
  test("contents jump that runs out of heap goes back to the saved page and saves nothing", [starvedTarget, nextPaint] {
    EpubReaderActivity r; starvedTarget(r);
    r.lastSavedSpineIndex = 1; r.lastSavedPage = 7; r.lastSavedPageCount = 19;
    r.currentSpineIndex = 2; r.pendingAnchor = "chuong-655";
    { RenderLock held; r.initialResume(0); }
    require(popupCount == 1 && buildErrors == 0, "starved first build did not show the memory notice");
    require(r.pendingAnchor.empty(), "the jump target stays pending and lands on the next chapter loaded");
    const auto landing = nextPaint(r);
    require(landing.spine == 1 && landing.page == 7, "the next paint shows page 0 of the target chapter");
    require(r.saveAttempts == 0, "page 0 of the target chapter was saved as the progress");
  });
  test("opening a book that runs out of heap keeps its place for the next try and saves nothing", [starvedTarget, nextPaint] {
    // Nothing saved in this visit yet: the place is the one the book opened at (chapter 3, page 12).
    EpubReaderActivity r; starvedTarget(r);
    r.currentSpineIndex = 3; r.nextPageNumber = 12;
    { RenderLock held; r.initialResume(12); }
    require(popupCount == 1 && buildErrors == 0, "starved open did not show the memory notice");
    const auto landing = nextPaint(r);
    require(landing.spine == 3 && landing.page == 12, "the next paint shows page 0 instead of the book's place");
    require(r.saveAttempts == 0, "page 0 was saved over the book's place");
  });
  test("radio still up after the release timeout goes straight to the memory notice", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.section->canPark = true;
    r.section->starveUntilRadioStopped = true; r.section->currentPage = r.section->pageCount;
    freeink::ble::stopForIdleResult = false;
    { RenderLock held; r.foreground(); }
    require(freeink::ble::stopForIdleCalls >= 1, "radio release not attempted");
    require(r.section && r.section->ticks == 1, "retried the build with the radio still holding its heap");
    require(buildErrors == 0 && popupCount == 1, "a radio still up did not fall back to the memory notice");
  });
  test("BLE enabled cold first page releases resident parser before first paint", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true;
    r.section->partial = false; r.section->oldPages = 0;
    r.section->builtPages = r.section->pageCount = 2; r.section->currentPage = 0;
    ESP.free = 95860; ESP.largest = 90100;
    { RenderLock held; r.foreground(); }
    require(!r.section->isBuilding(), "healthy-heap cold parser overlaps BLE init");
    require(r.section->isPartial() && r.section->pageCount == 2 && r.section->currentPage == 0, "cold page lost during BLE reservation");
  });
  test("BLE enabled defers background start even with abundant heap", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true;
    r.section->building = false; r.buildViewportWidth = 515;
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->starts == 0, "background parser raced BLE init");
  });
  test("BLE enabled on-demand crosses watermark then releases at healthy heap", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    freeink::ble::busyState = true;
    r.section->building = false; r.section->currentPage = 19; r.buildViewportWidth = 515;
    { RenderLock held; r.foreground(); }
    require(r.section->starts == 1 && r.section->pageCount > 19 && r.section->currentPage == 19, "foreground target blocked by BLE policy");
    require(!r.section->isBuilding(), "foreground parser remained resident with BLE enabled");
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->starts == 1 && r.section->suspends == 1, "on-demand restarted in background");
  });
  test("BLE policy releases active builder without latching disabled-BLE prefetch", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.buildViewportWidth = 515;
    freeink::ble::busyState = true;
    r.backgroundTick();
    require(!r.section->isBuilding() && r.section->suspends == 1, "active parser retained for BLE");
    freeink::ble::busyState = false;
    r.backgroundTick(); require(r.section->starts == 1 && r.section->isBuilding(), "BLE-only pause latched after user disabled BLE");
  });
#else
  test("unsupported BLE capability leaves healthy prefetch available", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.buildViewportWidth = 515;
    r.section->building = false;
    r.backgroundTick(); require(r.section->starts == 1 && r.section->ticks == 1, "unsupported radio setting disabled prefetch");
    { RenderLock held; r.foreground(); }
    require(r.section->isBuilding(), "unsupported radio setting suspended healthy parser");
  });
#endif
  return failures ? 1 : 0;
}
