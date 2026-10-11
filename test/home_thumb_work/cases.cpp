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
  std::puts("GREEN");
}
