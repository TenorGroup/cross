#pragma once
#include <HalStorage.h>
#include <mutex>
class PersistableStoreBase {
 protected:
  mutable std::mutex storeMutex;
 public:
  static bool readDocFromFile(const char* p, JsonDocument& d) {
    if(!Storage.exists(p)) return false;
    auto s=Storage.readFile(p); if(s.empty()) return false;
    ++io.parses; return !deserializeJson(d,s);
  }
  static bool writeDocToFile(const char* p,const JsonDocument& d) {
    Storage.mkdir("/.crosspoint"); std::string s; serializeJson(d,s); return Storage.writeFile(p,s);
  }
};
template <class T> class PersistableStore: public PersistableStoreBase {
 public: static T& getInstance(){static T x; return x;}
};
