int failures = 0;
int tests = 0;
void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
void test(const char* name, const std::function<void()>& run) {
  ++tests;
  nowMs = 1000;
  RenderLock::busy = false;
  activityManager.generation = 1;
  activityManager.toReaderPage = false;
  passTurns = {};
  passSideTurn = 0;
  events.clear();
  try { run(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error.what() << '\n';
  }
  RenderLock::busy = false;
}
bool logged(const char* phase, unsigned id) {
  const std::string prefix = std::string(phase) + " id=" + std::to_string(id) + " ";
  for (const auto& event : events)
    if (event.tag == "RDR_TRACE" && event.text.rfind(prefix, 0) == 0) return true;
  return false;
}
bool loggedDetail(const char* phase, unsigned id, const char* detail) {
  const std::string needle = std::string("detail=") + detail;
  const std::string prefix = std::string(phase) + " id=" + std::to_string(id) + " ";
  for (const auto& event : events)
    if (event.tag == "RDR_TRACE" && event.text.rfind(prefix, 0) == 0 &&
        event.text.find(needle) != std::string::npos) return true;
  return false;
}
bool noPhase(const char* phase) {
  const std::string prefix = std::string(phase) + " id=";
  for (const auto& event : events)
    if (event.tag == "RDR_TRACE" && event.text.rfind(prefix, 0) == 0) return false;
  return true;
}
std::string eventText(const char* phase, unsigned id) {
  const std::string prefix = std::string(phase) + " id=" + std::to_string(id) + " ";
  for (const auto& event : events)
    if (event.tag == "RDR_TRACE" && event.text.rfind(prefix, 0) == 0) return event.text;
  return {};
}

int main() {
  test("menu return to reader page requests its paint", [] {
    EpubReaderActivity reader; reader.pendingManualTurn = 1;
    activityManager.toReaderPage = true;
    reader.cancelManualForReaderMenu();
    require(reader.pendingManualTurn == 0 && reader.updates == 1, "menu return did not clear the queue and repaint");
  });
  test("external TryTake retains detection time and id", [] {
    ReaderActivity reader;
    require(reader.luotLatTrangNgoai(true), "external input rejected");
    RenderLock::busy = true;
    nowMs = 1400;
    require(!reader.processExternalPageTurn(), "busy external queue swallowed the tick's input");
    require(reader.modelPage == 1, "busy queue changed model");
    RenderLock::busy = false;
    nowMs = 1800;
    require(!reader.processExternalPageTurn(), "plain turns took the whole pass");
    require(reader.modelPage == 2 && reader.updates == 1, "external queue did not turn once");
    require(logged("DETECTED", 1) && logged("QUEUED", 1) && logged("APPLIED", 1),
            "external trace phases missing");
    require(loggedDetail("APPLIED", 1, "page"), "applied external metadata missing");
    const auto applied = eventText("APPLIED", 1);
    require(applied.find("gen=1") != std::string::npos &&
            applied.find("age_ms=800") != std::string::npos &&
            applied.find("heap=80000") != std::string::npos &&
            applied.find("largest=60000") != std::string::npos &&
            applied.find("min=48000") != std::string::npos, "applied log lacks generation, age or heap");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 1 && reader.appliedTurnTrace.detectedMs == 1000 &&
            std::string(reader.appliedTurnTrace.source) == "external", "external metadata changed during wait");
#endif
  });
  test("local TryTake retains detection time and id", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    require(!reader.pageTurn(false), "busy local turn mutated model");
    require(reader.modelPage == 1, "busy local turn changed model");
    RenderLock::busy = false;
    nowMs = 1700;
    require(!reader.processExternalPageTurn(), "plain turns took the whole pass");
    require(reader.modelPage == 0 && reader.trangDaLat == 1, "local queued turn not applied once");
    require(logged("DETECTED", 1) && logged("QUEUED", 1) && logged("APPLIED", 1),
            "local trace phases missing");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 1 && reader.appliedTurnTrace.detectedMs == 1000 &&
            std::string(reader.appliedTurnTrace.source) == "local", "local metadata changed during wait");
#endif
  });
  test("manual readiness delay retains external identity", [] {
    ReaderActivity reader;
    reader.ready = false;
    require(reader.luotLatTrangNgoai(true), "external input rejected");
    nowMs = 1450;
    require(!reader.processExternalPageTurn(), "unready turn swallowed the tick's input");
    require(reader.modelPage == 1 && noPhase("APPLIED"), "unready turn was applied");
    reader.ready = true;
    nowMs = 1900;
    require(!reader.processExternalPageTurn(), "plain turns took the whole pass");
    require(logged("APPLIED", 1) && eventText("APPLIED", 1).find("age_ms=900") != std::string::npos,
            "readiness wait lost original event time");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.detectedMs == 1000, "readiness wait changed detection time");
#endif
  });
  test("coalescing accounts for displaced and surviving events", [] {
    ReaderActivity reader;
    require(reader.luotLatTrangNgoai(true), "first external input rejected");
    nowMs = 1200;
    require(reader.luotLatTrangNgoai(false), "second external input rejected");
    require(!reader.processExternalPageTurn(), "plain turns took the whole pass");
    require(reader.modelPage == 0 && reader.trangDaLat == 1, "latest direction not applied once");
    require(logged("COALESCED", 1) && logged("QUEUED", 2) && logged("APPLIED", 2),
            "coalesced event disposition missing");
    require(!logged("APPLIED", 1), "displaced event was applied");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 2 && reader.appliedTurnTrace.detectedMs == 1200,
            "surviving event lost identity");
#endif
  });
  test("every local press that misses the lock turns one page", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    for (int i = 0; i < 3; ++i) require(!reader.pageTurn(false), "busy local turn mutated model");
    RenderLock::busy = false;
    require(!reader.processExternalPageTurn(), "plain turns took the whole pass");
    require(reader.modelPage == -2 && reader.trangDaLat == 3 && reader.updates == 1,
            "queued local presses did not land as three pages in one repaint");
    require(logged("MERGED", 1) && logged("MERGED", 2) && logged("APPLIED", 3), "merged presses lack disposition");
  });
  test("opposite local presses cancel out", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    reader.pageTurn(true);
    reader.pageTurn(false);
    require(reader.pendingExternalTurn == 0 && loggedDetail("DROPPED", 2, "cancelled"),
            "opposite local presses did not cancel");
    RenderLock::busy = false;
    require(!reader.processExternalPageTurn() && reader.modelPage == 1, "cancelled presses turned a page");
  });
  test("a remote report still replaces queued local presses", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    reader.pageTurn(true);
    reader.pageTurn(true);
    require(reader.luotLatTrangNgoai(false), "external input rejected");
    require(reader.pendingExternalTurn == -1, "remote report did not take the queue");
    RenderLock::busy = false;
  });
  test("queued turns are capped", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    for (int i = 0; i < 20; ++i) reader.pageTurn(true);
    require(reader.pendingExternalTurn == 8, "queue is not capped at eight turns");
    RenderLock::busy = false;
  });
  test("stale generation drops queued metadata", [] {
    ReaderActivity reader;
    require(reader.luotLatTrangNgoai(true), "external input rejected");
    ++activityManager.generation;
    require(!reader.processExternalPageTurn(), "stale generation consumed as turn");
    require(reader.modelPage == 1 && reader.trangDaLat == 0, "stale event changed model");
    require(logged("DROPPED", 1) && noPhase("APPLIED"), "stale trace disposition missing");
#if TRACE_PRESENT
    require(reader.pendingExternalTurnTrace.id == 0, "stale metadata retained");
#endif
  });
  test("rejected mutation has no applied trace", [] {
    ReaderActivity reader;
    reader.changed = false;
    require(!reader.pageTurn(true), "unchanged turn reported success");
    require(reader.modelPage == 1 && reader.trangDaLat == 0, "unchanged turn counted");
    require(logged("REJECTED", 1) && noPhase("APPLIED"), "rejection trace missing or applied");
  });
  test("new queue cannot overwrite applied render metadata", [] {
    ReaderActivity reader;
    require(reader.pageTurn(true), "first page turn rejected");
    require(logged("APPLIED", 1), "first applied trace missing");
    RenderLock::busy = true;
    nowMs = 1300;
    require(reader.luotLatTrangNgoai(false), "next external input rejected");
    require(!reader.processExternalPageTurn(), "held input swallowed the tick's input");
    require(reader.modelPage == 2, "queued input changed render model");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 1 && reader.appliedTurnTrace.detectedMs == 1000 &&
            reader.pendingExternalTurnTrace.id == 2 && reader.pendingExternalTurnTrace.detectedMs == 1300,
            "new queue overwrote applied metadata");
#endif
    require(logged("QUEUED", 2) && !logged("APPLIED", 2), "queued event was assigned render phase");
    RenderLock::busy = false;
  });
  test("EPUB guarded button input retains original id and time", [] {
    EpubReaderActivity reader;
    RenderLock::busy = true;
    reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == 1 && reader.modelPage == 1, "guarded EPUB input changed model");
    RenderLock::busy = false;
    nowMs = 1850;
    reader.drainManual();
    require(reader.pendingManualTurn == 0 && reader.modelPage == 2 && reader.updates == 1,
            "EPUB queued input failed to drain once");
    require(logged("DETECTED", 1) && loggedDetail("QUEUED", 1, "manual_guard") &&
            logged("APPLIED", 1) && eventText("APPLIED", 1).find("age_ms=850") != std::string::npos,
            "EPUB guarded input lost event metadata");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 1 && reader.appliedTurnTrace.detectedMs == 1000 &&
            std::string(reader.appliedTurnTrace.source) == "button", "EPUB event identity changed");
#endif
  });
  test("EPUB guarded inputs all count", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    nowMs = 1200;
    reader.manualInput(false, false, true, false);
    require(reader.pendingManualTurn == 2 && reader.modelPage == 1, "EPUB queue kept one press of two");
    reader.ready = true;
    nowMs = 1600;
    reader.drainManual();
    require(reader.modelPage == 3 && reader.trangDaLat == 2 && reader.updates == 1,
            "EPUB queued presses did not land as two pages in one repaint");
    require(logged("MERGED", 1) && logged("APPLIED", 2), "EPUB merged trace disposition missing");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id != 1, "EPUB merged press reported as the applied one");
#endif
  });
  test("EPUB opposite guarded presses cancel out", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    reader.manualInput(true, true, false, false);
    require(reader.pendingManualTurn == 0, "EPUB opposite presses did not add up to zero");
    require(loggedDetail("DROPPED", 2, "cancelled"), "EPUB cancelled queue lacks drop disposition");
  });
  test("EPUB press in the drain pass is not lost", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    reader.ready = true;
    nowMs = 1500;
    reader.drainThenInput(false, false, false, false);
    require(reader.modelPage == 3 && reader.trangDaLat == 2, "press read in the drain pass was dropped");
  });
  test("EPUB manual readiness handoff keeps queued identity", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, true);
    nowMs = 1400;
    reader.drainManual();
    require(reader.modelPage == 1 && noPhase("APPLIED"), "EPUB turn bypassed readiness guard");
    reader.ready = true;
    nowMs = 1750;
    reader.drainManual();
    require(reader.modelPage == 2 && logged("APPLIED", 1) &&
            eventText("APPLIED", 1).find("age_ms=750") != std::string::npos,
            "EPUB readiness handoff lost queued event time");
#if TRACE_PRESENT
    require(std::string(reader.appliedTurnTrace.source) == "tilt", "EPUB readiness changed source");
#endif
  });
  test("EPUB reader menu cancels queued manual input", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    reader.cancelManualForReaderMenu();
    reader.ready = true;
    nowMs = 1800;
    reader.drainManual();
    require(reader.pendingManualTurn == 0 && reader.modelPage == 1 && noPhase("APPLIED"),
            "cancelled EPUB input later changed page");
    require(loggedDetail("DROPPED", 1, "reader_menu"), "EPUB menu cancellation lacks drop event");
#if TRACE_PRESENT
    require(reader.pendingManualTurnTrace.id == 0, "EPUB menu retained cancelled metadata");
#endif
  });
  test("EPUB missing section drops queued manual trace", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    reader.section.reset();
    reader.ready = true;
    reader.drainManual();
    require(reader.pendingManualTurn == 0 && reader.modelPage == 1 && noPhase("APPLIED"),
            "EPUB missing-section queue changed page");
    require(loggedDetail("DROPPED", 1, "no_section"), "EPUB missing-section drop missing");
  });
  test("EPUB pause cancels queued manual input before resume", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == 1 && reader.modelPage == 1, "EPUB guard did not queue input");
    reader.onPause();
    reader.onResume();
    reader.ready = true;
    nowMs = 1800;
    reader.drainManual();
    require(reader.pendingManualTurn == 0 && reader.modelPage == 1 && noPhase("APPLIED"),
            "pre-pause EPUB input replayed after resume");
    require(loggedDetail("DROPPED", 1, "pause"), "EPUB paused queue lacks drop disposition");
#if TRACE_PRESENT
    require(reader.pendingManualTurnTrace.id == 0, "EPUB paused queue retained metadata");
#endif
  });
  test("EPUB direct exit disposes queued manual input", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == 1 && reader.modelPage == 1, "EPUB guard did not queue input");
    reader.onExit();
    require(reader.pendingManualTurn == 0 && reader.modelPage == 1 && noPhase("APPLIED"),
            "EPUB exit retained queued manual input");
    require(loggedDetail("DROPPED", 1, "exit"), "EPUB exit lacks drop disposition");
#if TRACE_PRESENT
    require(reader.pendingManualTurnTrace.id == 0, "EPUB exit retained queued metadata");
#endif
  });
  // A chapter still being laid out: page 5 is on screen, pages 0 to 5 are laid out, and the
  // chapter really ends at page 6. Five presses land during the paint: one reaches page 6 and
  // four go on into the next chapter. A batch that ran on past the laid-out pages would leave
  // the page far past the chapter's end, the paint would pull it back to page 6 and the presses
  // meant for the next chapter would be gone.
  test("EPUB queued presses wait at the page still being laid out", [] {
    EpubReaderActivity reader;
    reader.layout(5, 6, true);
    reader.ready = false;
    for (int i = 0; i < 5; ++i) reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == 5, "guarded presses were not queued");
    reader.ready = true;
    reader.drainManual();
    require(reader.section && reader.section->currentPage == 6 && reader.pendingManualTurn == 4,
            "queued turns ran past the pages laid out so far");
    reader.drainManual();
    require(reader.section->currentPage == 6 && reader.pendingManualTurn == 4,
            "queued turns went on before the paint laid the page out");
    reader.layout(6, 7, false);  // the paint lays the chapter out to its end
    reader.drainManual();
    require(!reader.section && reader.chapter == 1 && reader.pendingManualTurn == 3,
            "the rest did not wait for the next chapter");
    reader.layout(0, 10, false);
    reader.drainManual();
    require(reader.chapter == 1 && reader.section->currentPage == 3 && reader.pendingManualTurn == 0 &&
                reader.trangDaLat == 5,
            "a press queued during the chapter's layout was lost");
  });
  test("presses queued behind the render lock wait at the page still being laid out", [] {
    EpubReaderActivity reader;
    reader.layout(5, 6, true);
    RenderLock::busy = true;
    for (int i = 0; i < 5; ++i) require(!reader.pageTurn(true), "busy turn mutated the page");
    RenderLock::busy = false;
    reader.processExternalPageTurn();
    require(reader.section && reader.section->currentPage == 6 && reader.pendingExternalTurn == 4,
            "queued turns ran past the pages laid out so far");
    reader.processExternalPageTurn();
    require(reader.section->currentPage == 6 && reader.pendingExternalTurn == 4,
            "queued turns went on before the paint laid the page out");
    reader.layout(6, 7, false);
    reader.processExternalPageTurn();
    require(!reader.section && reader.chapter == 1 && reader.pendingExternalTurn == 3,
            "the rest did not wait for the next chapter");
    reader.layout(0, 10, false);
    reader.processExternalPageTurn();
    require(reader.section->currentPage == 3 && reader.pendingExternalTurn == 0 && reader.trangDaLat == 5,
            "a press queued during the chapter's layout was lost");
  });
  // The rare end of the case above: the chapter turns out to end right at the page the turn
  // stepped onto. That turn belongs to the next chapter; pulled back onto the last page it would
  // be counted without moving the reader.
  test("a turn past a chapter that ends right there goes on into the next chapter", [] {
    EpubReaderActivity reader;
    reader.layout(5, 6, true);
    reader.ready = false;
    for (int i = 0; i < 5; ++i) reader.manualInput(false, false, false, false);
    reader.ready = true;
    reader.drainManual();
    require(reader.section->currentPage == 6 && reader.pendingManualTurn == 4, "setup: the turn did not wait");
    const bool turnPastLaidOut = reader.pageAwaitsLayout();
    reader.layout(6, 6, false);  // the paint lays the chapter out: it ends before page 6
    reader.settleLaidOut(turnPastLaidOut);
    reader.layout(0, 10, false);
    reader.drainManual();
    require(reader.chapter == 1 && reader.section->currentPage == 4 && reader.pendingManualTurn == 0,
            "a turn past the chapter's end was pulled back onto its last page");
    require(reader.trangDaLat == 5, "pages counted differ from pages turned");
  });
  test("a turn past a chapter that ends right there, then one back, stays on its last page", [] {
    EpubReaderActivity reader;
    reader.layout(5, 6, true);
    reader.manualInput(false, false, false, false);  // onto page 6, not laid out yet
    reader.manualInput(true, true, false, false);    // back, queued behind it
    require(reader.section->currentPage == 6 && reader.pendingManualTurn == -1, "setup: the back press was not queued");
    const bool turnPastLaidOut = reader.pageAwaitsLayout();
    reader.layout(6, 6, false);
    reader.settleLaidOut(turnPastLaidOut);
    if (!reader.section) reader.layout(0, 10, false);  // the next chapter's paint
    reader.drainManual();
    if (!reader.section) reader.layout(5, 6, false);  // back into the chapter, on its last page
    require(reader.chapter == 0 && reader.section->currentPage == 5 && reader.pendingManualTurn == 0,
            "forward then back did not come back to the chapter's last page");
    require(reader.trangDaLat == 2, "pages counted differ from the turns made");
  });
  // A page that was set, not turned onto (a saved position past a shorter layout), still clamps.
  test("a page past a laid-out chapter that no turn stepped onto stays in the chapter", [] {
    EpubReaderActivity reader;
    reader.layout(6, 6, false);
    reader.settleLaidOut(false);
    require(reader.section && reader.chapter == 0 && reader.section->currentPage == 5,
            "a restored page left its chapter");
  });
  test("EPUB press on a page still being laid out joins the queue", [] {
    EpubReaderActivity reader;
    reader.layout(6, 6, true);  // a turn already stepped onto page 6; its paint has not run yet
    reader.manualInput(false, false, false, false);
    require(reader.section->currentPage == 6 && reader.pendingManualTurn == 1,
            "a press stepped further past the pages laid out so far");
  });
  // A chapter laid out to its end with no pages at all has nothing left to wait for.
  test("queued presses pass an empty chapter that is laid out", [] {
    EpubReaderActivity reader;
    reader.layout(0, 0, false);
    reader.ready = false;
    for (int i = 0; i < 2; ++i) reader.manualInput(false, false, false, false);
    reader.ready = true;
    reader.drainManual();
    require(!reader.section && reader.chapter == 1 && reader.pendingManualTurn == 1,
            "queued presses stuck in an empty chapter");
  });
  // TXT and XTC readers: the pass that applies the queue also reads this pass's press.
  test("press in the pass that applies the queue is not lost", [] {
    ReaderActivity reader;
    RenderLock::busy = true;
    require(!reader.pageTurn(true), "busy turn mutated the page");
    RenderLock::busy = false;
    passTurns.next = true;
    reader.loop();
    require(reader.modelPage == 3 && reader.trangDaLat == 2, "press read in the pass that applied the queue was dropped");
  });
  test("EPUB press in the pass that applies a remote turn is not lost", [] {
    EpubReaderActivity reader;
    require(reader.luotLatTrangNgoai(true), "remote turn rejected");
    reader.externalThenInput(false, false, false, false);
    require(reader.modelPage + reader.pendingManualTurn == 3,
            "button press read in the pass that applied a remote turn was dropped");
  });
  test("an exit that never happened lets the reader paint again", [] {
    // Back raised the flag; a sleep then replaced the Home transition and gave up, so the reader
    // stays. Left up, the flag drops every text paint and the panel keeps the old page.
    ReaderActivity reader;
    reader.leaving = true;
    reader.loop();
    require(!reader.leaving, "the reader stayed but still counts as leaving");
    require(reader.updates >= 1, "the page a dropped paint left behind is not painted again");
  });
  test("pause behind the reader menu keeps statistics in memory, once", [] {
    ReaderActivity reader;
    reader.statsDirty = true;
    reader.pauseKeepsStatsInRam = true;
    reader.onPause();
    activityManager.nextScreenFramed();
    require(reader.statsSaves == 0 && cardStatsSaves == 0, "pause behind the menu wrote statistics to the card");
    require(!reader.pauseKeepsStatsInRam, "the keep-in-memory flag outlived its pause");
    reader.onPause();
    require(reader.statsSaves == 0 && cardStatsSaves == 0,
            "the next pause wrote statistics before its screen's first frame");
    activityManager.nextScreenFramed();
    require(reader.statsSaves + cardStatsSaves == 1, "the next pause did not write statistics");
  });
  // "Giu nut lat trang khi doc" = side buttons, previous page (SIDE_HOLD_PREV, founder 26/10/2026). A side key turns
  // on its press, so a hold that only went back one page would land on the page the press started from: the hold
  // ends on the page BEFORE the one shown when the key went down, with one paint for the pages it goes back.
  test("XTC side hold after a forward press ends one page before the press", [] {
    ReaderActivity reader;
    reader.modelPage = 5;
    reader.mappedInput.sideKeyDown = true;
    passTurns.next = true;
    passTurns.sideTurn = 1;
    reader.loop();
    require(reader.modelPage == 6 && reader.updates == 1, "setup: the press did not turn on its press");
    reader.mappedInput.sideKeyDown = false;
    passTurns.sidePrev = true;
    reader.loop();
    require(reader.modelPage == 4, "the hold did not end on the page before the one the press started on");
    require(reader.updates == 2, "the two pages back did not go out as one paint");
    require(reader.sideHoldTurn == 0, "the press's turn outlived its hold");
  });
  test("XTC side hold after a back press goes back no further", [] {
    ReaderActivity reader;
    reader.modelPage = 5;
    reader.mappedInput.sideKeyDown = true;
    passTurns.prev = true;
    passTurns.sideTurn = -1;
    reader.loop();
    require(reader.modelPage == 4, "setup: the back press did not turn");
    reader.mappedInput.sideKeyDown = false;
    passTurns.sidePrev = true;
    reader.loop();
    require(reader.modelPage == 4 && reader.updates == 1, "the hold went past the page before the press");
  });
  test("XTC side hold after a press that turned nothing goes back one page", [] {
    ReaderActivity reader;
    reader.modelPage = 5;
    reader.sideHoldTurn = 1;  // an earlier press, then a side key went down that something else took
    reader.mappedInput.sideKeyDown = true;
    reader.loop();
    reader.mappedInput.sideKeyDown = false;
    passTurns.sidePrev = true;
    reader.loop();
    require(reader.modelPage == 4, "an earlier press counted for this hold");
  });
  test("XTC side hold from the end of the book lands before the last page", [] {
    ReaderActivity reader;
    reader.modelPage = 9;  // the last page
    reader.mappedInput.sideKeyDown = true;
    passTurns.next = true;
    passTurns.sideTurn = 1;
    reader.loop();
    reader.atEnd = true;  // page 10 is the end screen
    reader.mappedInput.sideKeyDown = false;
    passTurns.sidePrev = true;
    reader.loop();
    require(!reader.atEnd && reader.modelPage == 8, "the hold from the end did not land on the page before the last");
  });
  test("XTC side hold on the end screen after its press goes back to the last page", [] {
    ReaderActivity reader;
    reader.modelPage = 10;
    reader.atEnd = true;  // the end screen; its menu took the press
    reader.mappedInput.sideKeyDown = true;
    reader.loop();
    reader.mappedInput.sideKeyDown = false;
    passTurns.sidePrev = true;
    reader.loop();
    require(!reader.atEnd && reader.modelPage == 9 && reader.updates == 1, "the end screen did not go back to the last page");
  });
  test("EPUB side hold after a forward press goes back two pages as one move", [] {
    EpubReaderActivity reader;
    reader.chapter = 1;
    reader.layout(5, 10, false);
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    require(reader.section->currentPage == 6 && reader.sideHoldTurn == 1, "setup: the press did not turn");
    reader.sideHoldToPreviousPage();
    require(reader.pendingManualTurn == -2, "the hold did not queue two pages back");
    reader.drainManual();
    require(reader.chapter == 1 && reader.section->currentPage == 4 && reader.pendingManualTurn == 0 &&
                reader.updates == 2,
            "two pages back did not land as one move");
  });
  test("EPUB side hold across the start of a chapter paints only the page it ends on", [] {
    EpubReaderActivity reader;
    reader.chapter = 1;
    reader.layout(0, 10, false);  // the first page of chapter 1
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    reader.sideHoldToPreviousPage();
    reader.drainManual();
    require(!reader.section && reader.chapter == 0 && reader.pendingManualTurn == 0,
            "the hold did not go back into chapter 0");
    reader.layout(7, 8, false);  // the paint lays chapter 0 out on its last page
    require(reader.section->currentPage == 7, "the hold did not end on chapter 0's last page");
  });
  test("EPUB side hold from the first page of a chapter goes back into the one before", [] {
    EpubReaderActivity reader;
    reader.layout(7, 8, false);  // the last page of chapter 0
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    require(!reader.section && reader.chapter == 1, "setup: the press did not go on into chapter 1");
    reader.layout(0, 10, false);  // its paint
    reader.sideHoldToPreviousPage();
    reader.drainManual();
    require(!reader.section && reader.chapter == 0 && reader.pendingManualTurn == -1,
            "the turn back into chapter 0 did not wait for its layout");
    reader.layout(7, 8, false);  // laid out on its last page; queued, so this paint is dropped (nextScreenWaiting)
    reader.drainManual();
    require(reader.chapter == 0 && reader.section->currentPage == 6 && reader.pendingManualTurn == 0,
            "the hold did not end on the page before the press");
  });
  test("EPUB side hold during the press's paint lands with it", [] {
    EpubReaderActivity reader;
    reader.layout(5, 10, false);
    reader.ready = false;  // the press's turn is still in its paint
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == 1 && reader.sideHoldTurn == 1, "setup: the press was not queued");
    reader.sideHoldToPreviousPage();
    reader.ready = true;
    reader.drainManual();
    require(reader.section->currentPage == 4 && reader.trangDaLat == 1, "the queued press and the hold did not add up");
  });
  test("EPUB side hold does not go before the first page", [] {
    EpubReaderActivity reader;
    reader.layout(0, 10, false);  // the first page of the book
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    reader.sideHoldToPreviousPage();
    reader.drainManual();
    require(reader.chapter == 0 && reader.section->currentPage == 0 && reader.pendingManualTurn == 0,
            "the hold went before the first page");
  });
  test("EPUB side hold from the end of the book lands before the last page", [] {
    EpubReaderActivity reader;
    reader.chapter = 1;
    reader.layout(9, 10, false);  // the last page of the book
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    require(reader.atEnd, "setup: the press did not reach the end of the book");
    reader.sideHoldToPreviousPage();
    require(!reader.atEnd && reader.pendingManualTurn == -1, "the end did not go back to the last page");
    reader.drainManual();
    require(reader.section->currentPage == 8, "the hold did not end on the page before the last");
  });
  test("EPUB side hold with the queue already full goes back no further than the queue", [] {
    EpubReaderActivity reader;
    reader.layout(5, 20, false);
    reader.ready = false;  // the paint of an earlier press: every press joins the queue
    reader.pendingManualTurn = reader.MAX_QUEUED_TURNS;
    passSideTurn = 1;
    reader.manualInput(false, false, false, false);
    require(reader.pendingManualTurn == reader.MAX_QUEUED_TURNS, "setup: the press changed a full queue");
    reader.sideHoldToPreviousPage();
    // The press added no page (the queue was full), so the hold goes back one, not two.
    require(reader.pendingManualTurn == reader.MAX_QUEUED_TURNS - 1, "the hold went back for a page the press never queued");
  });
  std::cout << "RESULT " << tests - failures << "/" << tests << " passed\n";
  return failures ? 1 : 0;
}
