#pragma once
// Host stand-in: saveToFile() reports what the case sets, so a refused card write can be staged.
#include <ArduinoJson.h>

namespace recentStoreFake {
inline bool saveWorks = true;
inline int saves = 0;
}  // namespace recentStoreFake

template <typename T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
  bool saveToFile() const {
    ++recentStoreFake::saves;
    return recentStoreFake::saveWorks;
  }
};
