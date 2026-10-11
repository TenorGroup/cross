int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string name = argv[1];
  HomeActivity h;
  if (name == "power") {
    h.touchThumbCount = 1;
    h.writeMissingThumb(0);
    h.ink = false;
    h.writeMissingThumb(0);
    h.holderCount = 1;
    assert(h.tickHolders());
    assert(decodes == 2 && holders == 1 && fullSpeed && powerLocks == 0);
  }
  if (name == "queue") {
    for (int i = 0; i < 4; ++i) h.queueTouchThumb(i, 100 + i, 150 + i);
    h.writeMissingThumb(0);
    assert(decodes == 1 && h.touchThumbCount == 3 && h.thumbWanted == 0 && h.updates == 0);
    mappedInput.pressed = true;
    h.writeMissingThumb(0);
    assert(decodes == 1 && h.touchThumbCount == 3);
    mappedInput.pressed = false;
    gpio.held = true;
    h.writeMissingThumb(0);
    assert(decodes == 1 && h.touchThumbCount == 3);
    gpio.held = false;
    for (int i = 0; i < 2; ++i) h.writeMissingThumb(0);
    assert(h.updates == 0);
    nextWrite = false;
    h.writeMissingThumb(0);
    assert(decodes == 4 && h.touchThumbCount == 0 && h.updates == 1);
    std::fill(std::begin(thumbTried), std::end(thumbTried), 0);
    HomeActivity failed;
    for (int i = 0; i < 4; ++i) failed.queueTouchThumb(i, 100 + i, 150 + i);
    for (int i = 0; i < 4; ++i) failed.writeMissingThumb(0);
    assert(failed.updates == 0);
  }
  if (name == "heap") {
    h.ink = false;
    tenorchrome::kTouchShell = false;
    ESP.heap = 10000;
    h.writeMissingThumb(0);
    assert(h.updates == 0 && popups == 0 && decodes == 0);
    ESP.heap = 200000;
    h.writeMissingThumb(0);
    assert(h.updates == 1 && popups == 1 && decodes == 1);
  }
  std::puts("GREEN");
}
