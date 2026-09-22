#pragma once

#include <FS.h>
#include <common/FsApiConstants.h>
#include <vector>

class SDCardManager {
 public:
  static SDCardManager& getInstance() { static SDCardManager manager; return manager; }
  bool begin() { return true; }
  bool ready() const { return true; }
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
