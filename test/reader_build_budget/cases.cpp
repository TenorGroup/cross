int failures = 0;
template<class F> void test(const char* name, F fn) {
  ESP = {}; SETTINGS = {}; pageReads = {}; RenderLock::busy = false;
  freeink::ble::busyState = false;
  freeink::ble::initializingState = false;
  freeink::ble::readerStartDeferredState = false;
  freeink::ble::idleStoppedState = false;
  freeink::ble::stopForIdleCalls = freeink::ble::rearmRequests = 0;
  clockMs = 1000; popupCount = buildErrors = 0; popupAtMs = 0; thumbs = {}; tenorchrome::enabledState = true;
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
  // Heap values below are the ones the device logged right after the first page of a
  // cold open painted: PAINT_COMPLETE rid=1 heap=116768 largest=90100.
  test("cover thumbnail leaves the open path and runs on an idle pass", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    // One look per height: the card's own and the theme's.
    require(thumbs.existsChecks == 2, "open path stopped looking for the cover thumbnails");
    require(thumbs.generated == 0, "open path still paid for the cover thumbnail");
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    clockMs += 500;  // past IDLE_PREWARM_DEBOUNCE_MS, first page is on the panel
    r.idleStep();
    // One per pass, so input is taken between two seconds-long decodes; the card's own first.
    require(thumbs.heights == std::vector<int>{356}, "idle pass did not generate the card-sized thumbnail first");
    // A loan returns the framebuffer white and nothing repaints it on this path.
    require(thumbs.loans == 0, "idle thumbnail borrowed the framebuffer under a live page");
    clockMs += 500;
    r.idleStep();
    require((thumbs.heights == std::vector<int>{356, 226}), "second idle pass did not generate the theme thumbnail");
    clockMs += 500;
    r.idleStep();
    require(thumbs.generated == 2, "deferred thumbnail ran again on a later idle pass");
  });
  test("book opened before the card thumbnail gets it on its next idle pass", [] {
    EpubReaderActivity r; thumbs.onCard = {226};
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    for (int pass = 0; pass < 3; ++pass) {
      clockMs += 500;
      r.idleStep();
    }
    require(thumbs.heights == std::vector<int>{356}, "only the missing card thumbnail should be generated");
  });
  test("other themes keep the theme thumbnail alone", [] {
    EpubReaderActivity r; tenorchrome::enabledState = false;
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    require(thumbs.existsChecks == 1, "a theme without the card looked for the card thumbnail");
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    for (int pass = 0; pass < 3; ++pass) {
      clockMs += 500;
      r.idleStep();
    }
    require(thumbs.heights == std::vector<int>{226}, "a theme without the card paid for the card thumbnail");
  });
  test("deferred thumbnail waits for heap and for the first page", [] {
    EpubReaderActivity r; r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    ESP.free = 116768; ESP.largest = 90100;
    r.idleStep();
    require(thumbs.generated == 0, "thumbnail ran before the first page was painted");
    r.lastRenderCompleteMs = millis();
    clockMs += 500;
    ESP.free = 53364; ESP.largest = 28660;  // deep in a chapter, parser resident
    r.idleStep();
    require(thumbs.generated == 0, "thumbnail ran below the idle heap budget");
    ESP.free = 116768; ESP.largest = 90100;
    r.idleStep();
    require(thumbs.generated == 1, "thumbnail never recovered once heap returned");
    // The second height waits for the same budget.
    clockMs += 500;
    ESP.free = 53364; ESP.largest = 28660;
    r.idleStep();
    require(thumbs.generated == 1, "second thumbnail ran below the idle heap budget");
  });
  test("existing cover thumbnail asks for nothing on either path", [] {
    EpubReaderActivity r; thumbs.fileOnCard = true;
    r.section->building = r.section->partial = false;
    r.section->currentPage = 4; r.section->pageCount = r.section->oldPages = 5;
    r.openThumbStep();
    ESP.free = 116768; ESP.largest = 90100;
    r.lastRenderCompleteMs = millis();
    clockMs += 500;
    r.idleStep();
    require(thumbs.generated == 0, "warm open regenerated an existing thumbnail");
    require(thumbs.loans == 0, "warm open borrowed the framebuffer for nothing");
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
