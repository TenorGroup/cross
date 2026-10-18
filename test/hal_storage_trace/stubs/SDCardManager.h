#pragma once

#include <FS.h>
#include <HwLog.h>
#include <common/FsApiConstants.h>
#include <vector>

class SDCardManager {
 public:
  static SDCardManager& getInstance() { static SDCardManager manager; return manager; }
  // The first beginFailures calls fail, as a card that does not answer.
  inline static int beginFailures = 0;
  bool begin() {
    const bool ok = beginFailures <= 0;
    if (!ok) --beginFailures;
    hwLog.push_back(ok ? "begin ok" : "begin fail");
    return ok;
  }
  bool ready() const { return true; }
  bool freeSpace(uint64_t& bytes, uint32_t& clusterBytes) {
    bytes = 1ULL << 30;
    clusterBytes = 4096;
    return true;
  }
  void shutdown() {}
  std::vector<String> listFiles(const char*, int) { return {}; }
  String readFile(const char*) { return {}; }
  bool readFileToStream(const char*, Print&, size_t) { return true; }
  size_t readFileToBuffer(const char*, char*, size_t, size_t) { return 0; }
  bool writeFile(const char*, const String&) { return true; }
  bool ensureDirectoryExists(const char*) { return true; }
  FsFile open(const char* path, oflag_t) { return openFile(path); }
  bool mkdir(const char*, bool) { return true; }
  bool exists(const char*) { return true; }
  bool remove(const char*) { return true; }
  bool rename(const char*, const char*) { return true; }
  bool rmdir(const char*) { return true; }
  bool openFileForRead(const char*, const char* path, FsFile& file) {
    file = openFile(path);
    return file.isOpen();
  }
  bool openFileForWrite(const char*, const char* path, FsFile& file) {
    file = openFile(path);
    return file.isOpen();
  }
  bool removeDir(const char*) { return true; }

 private:
  FsFile openFile(const char* path) { return std::string(path) == "/missing" ? FsFile() : FsFile(path); }
};
