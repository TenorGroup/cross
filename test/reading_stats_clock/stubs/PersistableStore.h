#pragma once

#include <ArduinoJson.h>

#include <mutex>

class PersistableStoreBase {
 protected:
  mutable std::mutex storeMutex;

 public:
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
