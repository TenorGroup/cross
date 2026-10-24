#pragma once
#include <ArduinoJson.h>

#include <string>
// What the credential store takes from its base: the singleton, the password in the file, and a resave request.
template <typename T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
  void saveToFile() {}

 protected:
  void requestResave() {}
  static std::string extractPassword(JsonVariantConst doc, bool&) { return doc["password"] | ""; }
};
