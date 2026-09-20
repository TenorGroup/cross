int failures = 0;
template<class F> void test(const char* name, F fn) {
  ESP = {}; SETTINGS = {}; RenderLock::busy = false;
  try { fn(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
int main() {
  test("low heap releases active build and keeps larger partial/current page", [] {
    EpubReaderActivity r; ESP.free = 29100; ESP.largest = 17396;
    r.backgroundTick();
    require(!r.section->isBuilding(), "parser/CSS/LUT retained after gate fails");
    require(r.section->pageCount == 19 && r.section->currentPage == 4, "lost readable partial or position");
    require(r.section->suspends == 1, "expected exactly one suspension");
    require(!r.skipLoopDelay(), "paused build busy-spins");
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
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  test("BLE enabled cold first page releases resident parser before first paint", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    r.section->partial = false; r.section->oldPages = 0;
    r.section->builtPages = r.section->pageCount = 2; r.section->currentPage = 0;
    ESP.free = 95860; ESP.largest = 90100;
    { RenderLock held; r.foreground(); }
    require(!r.section->isBuilding(), "healthy-heap cold parser overlaps BLE init");
    require(r.section->isPartial() && r.section->pageCount == 2 && r.section->currentPage == 0, "cold page lost during BLE reservation");
  });
  test("BLE enabled defers background start even with abundant heap", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    r.section->building = false; r.buildViewportWidth = 515;
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->starts == 0, "background parser raced BLE init");
  });
  test("BLE enabled on-demand crosses watermark then releases at healthy heap", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true;
    r.section->building = false; r.section->currentPage = 19; r.buildViewportWidth = 515;
    { RenderLock held; r.foreground(); }
    require(r.section->starts == 1 && r.section->pageCount > 19 && r.section->currentPage == 19, "foreground target blocked by BLE policy");
    require(!r.section->isBuilding(), "foreground parser remained resident with BLE enabled");
    for (int n = 0; n < 30; ++n) r.backgroundTick();
    require(r.section->starts == 1 && r.section->suspends == 1, "on-demand restarted in background");
  });
  test("BLE policy releases active builder without latching disabled-BLE prefetch", [] {
    EpubReaderActivity r; SETTINGS.blePageTurnerEnabled = true; r.buildViewportWidth = 515;
    r.backgroundTick();
    require(!r.section->isBuilding() && r.section->suspends == 1, "active parser retained for BLE");
    SETTINGS.blePageTurnerEnabled = false;
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
