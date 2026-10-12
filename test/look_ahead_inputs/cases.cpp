int delivered=0;
bool deliver(bleturner::Action action) {
  require(!RenderLock::busy, "BLE delivery ran inside the build lock");
  if (action != bleturner::Action::NextPage) return false;
  ++delivered;
  return bleDeliver(action);
}
bleturner::Heap heap() { return {80000, 60000}; }
bool yes() { return true; }
bool no() { return false; }
void nothing() {}
void hold(bool) {}
void log(bool, const char*, va_list) {}
const bleturner::Host host{heap, yes, deliver, yes, no, nothing, hold, log, nullptr};

void poll(EpubReaderActivity& reader) {
  require(!RenderLock::busy, "main input poll ran inside the build lock");
  ++mainPolls;
  gpio.update();
  bleturner::Scene scene{};
  scene.where=bleturner::Where::Reader;
  scene.visit=1;
  scene.pageShown=true;
  bleturner::tick(scene);
  reader.backgroundTick();
}

void check(const std::string& kind) {
  fakeble::resetWorld();
  bleturner::detail::resetForTests();
  pins.fill(HIGH);
  adc.fill(4095);
  gpio=HalGPIO{};
  pressPlan={};
  InputManager::s_buttonHook=nullptr;
  BoardConfig::ACTIVE={};
  if (kind != "x4pro_gpio") BoardConfig::ACTIVE.inputStyle=BoardConfig::InputStyle::XteinkAdcLadder;
  const int map=fakeble::addCharacteristic(0x2a4b, true);
  fakeble::setCharacteristicValue(map, hidtest::kBootKeyboard, sizeof hidtest::kBootKeyboard);
  fakeble::addCharacteristic(0x2a4e, false, true);
  const int report=fakeble::addCharacteristic(0x2a4d, false, false, true);
  require(fakeble::beginHost(), "BLE host failed to begin");
  require(fakeble::connectTo("AA:BB:CC:DD:EE:FF") == fakeble::LinkResult::Connected, "BLE connect failed");
  require(fakeble::isSubscribed(report), "HID notification is not subscribed");
  bleturner::Config config{};
  config.enabled=1;
  bleturner::begin(host, config);
  EpubReaderActivity reader;
  activityManager.currentActivity.value=&reader;
  delivered=mainPolls=0;
  lastBuildPoll=-1;
  reader.lastRenderCompleteMs=millis()-500;
  reader.radioSettledMs=millis()-500;
  reader.buildViewportWidth=480;
  auto& section=*reader.section;
  section.currentPage=4;
  section.pageCount=section.oldPages=section.builtPages=5;
  section.parked=section.canPark=true;
  section.ticksPerPage=kind == "quiet_completion" ? 2 : 100;
  section.tickMs=20;
  const bool bleInput=kind == "raw_hid_ble" || kind == "completion_then_ble";
  if (kind == "completion_then_ble") {
    section.pageCount=section.oldPages=section.builtPages=6;
    section.ticksPerPage=1;
  }
  if (kind == "quiet_completion") {
    quantumHook={};
    for (int pass=0; pass<16 && reader.lookAheadPage < 0; ++pass) {
      const int before=section.ticks;
      poll(reader);
      require(section.ticks-before <= 1, "quiet pass exceeded one quantum");
      if (section.pageCount <= section.currentPage + reader.LOOK_AHEAD_PAGES)
        require(reader.lookAheadPage == -1, "completion latch set before target pages exist");
      fakeble::advanceMillis(6);
    }
    require(section.pageCount > section.currentPage + reader.LOOK_AHEAD_PAGES, "quiet look-ahead did not complete across passes");
    require(reader.lookAheadPage == section.currentPage, "completed look-ahead did not latch current page");
    require(section.parks <= 1, "quiet look-ahead reparks parser between quanta");
    std::cout << "PASS " << kind << " ticks=" << section.ticks << " polls=" << mainPolls << '\n';
    fakeble::endHost();
    return;
  }
  bool injected=false;
  const unsigned long firstQuantumStarted=millis();
  if (kind == "planned_probe") gpio.injectPresses(InputManager::BTN_DOWN, 150, 1, 10);
  quantumHook=[&] {
    if (injected) return;
    require(millis()-firstQuantumStarted >= section.tickMs, "build quantum did not advance input clock");
    injected=true;
    if (kind == "physical_adc") adc[2]=5;
    if (kind == "x4pro_gpio") {
      pins[7]=LOW;
      require(!gpio.rawInputActive(), "X4 Pro raw helper boundary changed; revisit GPIO test");
    }
    if (bleInput) {
      const uint8_t frame[8]={0,0,0x4f,0,0,0,0,0};
      require(fakeble::notify(report, frame, sizeof frame), "raw HID notification failed");
      require(reader.pendingExternalTurn == 0 && delivered == 0, "BLE delivered before main loop tick");
    }
  };
  try {
    poll(reader);
    require(injected && section.ticks == 1, "first main pass did not yield after one quantum");
    if (kind == "completion_then_ble")
      require(reader.lookAheadPage == section.currentPage, "completing quantum did not latch before BLE poll");
    else
      require(reader.lookAheadPage == -1, "unfinished look-ahead set completion latch");
    require(!RenderLock::busy, "look-ahead held render lock across main passes");
    bool inputSeen=false;
    for (int pass=0; pass<4 && !inputSeen; ++pass) {
      fakeble::advanceMillis(6);
      const int before=section.ticks;
      poll(reader);
      inputSeen=gpio.wasAnyPressed() || reader.pendingExternalTurn != 0;
      require(section.ticks-before <= 1, "main pass exceeded one build quantum");
      if (inputSeen) require(section.ticks == before, "build ran in accepted input pass");
      require(reader.lookAheadPage == -1, "interrupted look-ahead latched completion");
    }
    require(inputSeen, "input never reached reader after yielding");
    if (bleInput) require(delivered == 1 && reader.pendingExternalTurn == 1, "raw HID failed to queue page turn through runtime");
    else require(gpio.inputMgr.isPressed(InputManager::BTN_DOWN), "electrical input did not reach SDK debounce");
    std::cout << "PASS " << kind << " ticks=" << section.ticks << " polls=" << mainPolls << " latch=" << reader.lookAheadPage << '\n';
  } catch (...) {
    quantumHook={};
    fakeble::endHost();
    throw;
  }
  quantumHook={};
  fakeble::endHost();
}
int main() {
  int failures=0;
  for (const auto* name : {"physical_adc", "planned_probe", "raw_hid_ble", "x4pro_gpio", "quiet_completion", "completion_then_ble"}) {
    try { check(name); }
    catch (const std::exception& error) { quantumHook={}; fakeble::endHost(); ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
