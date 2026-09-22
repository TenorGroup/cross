int failures = 0;
int tests = 0;
void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
void test(const char* name, const std::function<void()>& run) {
  ++tests;
  nowMs = 1000;
  RenderLock::busy = false;
  activityManager.generation = 1;
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
  test("external TryTake retains detection time and id", [] {
    ReaderActivity reader;
    require(reader.luotLatTrangNgoai(true), "external input rejected");
    RenderLock::busy = true;
    nowMs = 1400;
    require(reader.processExternalPageTurn(), "busy external queue was not deferred");
    require(reader.modelPage == 1, "busy queue changed model");
    RenderLock::busy = false;
    nowMs = 1800;
    require(reader.processExternalPageTurn(), "external queue was not drained");
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
    require(reader.processExternalPageTurn(), "local queued turn was not drained");
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
    require(reader.processExternalPageTurn(), "unready turn was not deferred");
    require(reader.modelPage == 1 && noPhase("APPLIED"), "unready turn was applied");
    reader.ready = true;
    nowMs = 1900;
    require(reader.processExternalPageTurn(), "ready turn did not drain");
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
    require(reader.processExternalPageTurn(), "coalesced queue did not drain");
    require(reader.modelPage == 0 && reader.trangDaLat == 1, "latest direction not applied once");
    require(logged("COALESCED", 1) && logged("QUEUED", 2) && logged("APPLIED", 2),
            "coalesced event disposition missing");
    require(!logged("APPLIED", 1), "displaced event was applied");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 2 && reader.appliedTurnTrace.detectedMs == 1200,
            "surviving event lost identity");
#endif
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
    require(reader.processExternalPageTurn(), "next input was not held by lock");
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
  test("EPUB guarded inputs coalesce with disposition", [] {
    EpubReaderActivity reader;
    reader.ready = false;
    reader.manualInput(false, false, false, false);
    nowMs = 1200;
    reader.manualInput(true, true, true, false);
    require(reader.pendingManualTurn == -1 && reader.modelPage == 1, "EPUB queue lost latest direction");
    reader.ready = true;
    nowMs = 1600;
    reader.drainManual();
    require(reader.modelPage == 0 && reader.trangDaLat == 1, "EPUB coalesced turn applied incorrectly");
    require(logged("COALESCED", 1) && logged("APPLIED", 2) && !logged("APPLIED", 1),
            "EPUB displaced or surviving trace disposition missing");
#if TRACE_PRESENT
    require(reader.appliedTurnTrace.id == 2 && reader.appliedTurnTrace.detectedMs == 1200 &&
            std::string(reader.appliedTurnTrace.source) == "touch", "EPUB latest input metadata changed");
#endif
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
  std::cout << "RESULT " << tests - failures << "/" << tests << " passed\n";
  return failures ? 1 : 0;
}
