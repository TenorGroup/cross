#pragma once

// Host stand-in for lib/hal/HalStorage.h, which needs Print/freertos/String and
// the ESP SD driver. Only the members FontInstaller uses exist here, and every
// call is recorded so a test can prove that a rejected family name or path
// never probes, creates or deletes anything on the card.
//
// No .cpfont payload is modelled: this target covers the name/path contract,
// not FontInstaller::validateCpfontFile().

#include <cstddef>
#include <string>
#include <vector>

struct HalFile {
  size_t read(void*, size_t) { return 0; }
  void close() {}
};

struct HostStorage {
  // Every call in order, as "<op>:<path>" - the observable storage trace.
  std::vector<std::string> calls;
  // Answer given to exists(): true = every path is already on the card.
  bool present = false;
  // Force removeDir() to fail, for the deleteFamily error path.
  bool failRemove = false;

  bool exists(const char* path) {
    calls.emplace_back(std::string("exists:") + path);
    return present;
  }
  bool mkdir(const char* path) {
    calls.emplace_back(std::string("mkdir:") + path);
    return true;
  }
  bool removeDir(const char* path) {
    calls.emplace_back(std::string("remove:") + path);
    return !failRemove;
  }
  bool openFileForRead(const char*, const char* path, HalFile&) {
    calls.emplace_back(std::string("read:") + path);
    return false;
  }
};

inline HostStorage Storage;
