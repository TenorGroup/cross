#pragma once

#include <ArduinoJson.h>
#include <fcntl.h>

#include <cstddef>
#include <cstdint>
#include <string>

#define LOG_ERR(...) ((void)0)

void delay(unsigned long);

class HalFile {
 public:
  explicit operator bool() const { return false; }
  bool isDirectory() const { return false; }
  size_t size() const { return 0; }
  int read(void*, size_t) { return -1; }
  size_t write(const char*, size_t) { return 0; }
  bool close() { return false; }
  HalFile openNextFile() { return {}; }
  void getName(char*, size_t) const {}
};

struct FakeStorage {
  HalFile open(const char*, int = O_RDONLY) const { return {}; }
  bool exists(const char*) const { return false; }
  bool remove(const char*) const { return false; }
  bool rename(const char*, const char*) const { return false; }
  bool ensureDirectoryExists(const char*) const { return false; }
  bool mkdir(const char*) const { return false; }
};

inline FakeStorage Storage;
