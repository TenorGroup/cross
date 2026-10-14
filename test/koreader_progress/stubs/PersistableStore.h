#pragma once
// Only what the settings class needs to be named and reached as a singleton.
template <typename T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
};
