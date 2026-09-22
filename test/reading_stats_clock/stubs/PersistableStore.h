#pragma once

#include <ArduinoJson.h>

#include <mutex>

class PersistableStoreBase {
 protected:
  mutable std::mutex storeMutex;

 public:
  enum class ReadResult { Ready, Invalid, Unavailable };
  static ReadResult readDocFromFileStatus(const char*, JsonDocument&, size_t = 0) { return ReadResult::Invalid; }
  static bool readDocFromFile(const char*, JsonDocument&) { return false; }
  static bool writeDocToFile(const char*, const JsonDocument&) { return false; }
};

template <class T>
class PersistableStore : public PersistableStoreBase {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
};
