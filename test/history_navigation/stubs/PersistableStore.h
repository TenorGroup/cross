#pragma once
#include <HalStorage.h>
#include <mutex>
class PersistableStoreBase {
 protected:
  mutable std::mutex storeMutex;
 public:
  static bool readDocFromFile(const char* p, JsonDocument& d) {
    ++io.readDocs;
    bool unavailable=false;
    const auto parse=[&](const char* candidate) {
      unavailable=false;
      if(!Storage.exists(candidate)) return false;
      const auto json=Storage.readFile(candidate);
      d.clear();
      if(json.empty()) {
        auto probe=Storage.open(candidate);
        if(!probe) { unavailable=true;return false; }
        const bool emptyFile=probe.size()==0;
        unavailable=!probe.close() || !emptyFile;return false;
      }
      ++io.parses;
      const auto error=deserializeJson(d,json);
      unavailable=error==DeserializationError::NoMemory || d.overflowed();
      return !error && !unavailable;
    };
    if(parse(p)) return true;
    if(unavailable) return false;
    const std::string backup=std::string(p)+".davbak";
    if(!parse(backup.c_str())) { d.clear();return false; }
    if(Storage.exists(p) && !Storage.remove(p)) return true;
    Storage.rename(backup.c_str(),p);
    return true;
  }

  static bool writeDocToFile(const char* p,const JsonDocument& d) {
    Storage.mkdir("/.crosspoint"); std::string s; serializeJson(d,s); return Storage.writeFile(p,s);
  }
};
template <class T> class PersistableStore: public PersistableStoreBase {
 public: static T& getInstance(){static T x; return x;}
};
