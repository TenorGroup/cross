#pragma once
#include <HalStorage.h>
#include <mutex>
class PersistableStoreBase {
 protected:
  mutable std::mutex storeMutex;
 public:
  enum class ReadResult { Ready, Invalid, Unavailable };
  static bool readDocFromFile(const char* p, JsonDocument& d) {
    return readDocFromFileStatus(p,d)==ReadResult::Ready;
  }
  static ReadResult readDocFromFileStatus(const char* p, JsonDocument& d, size_t maxBytes=0) {
    ++io.readDocs;
    const auto parse=[&](const char* candidate) {
      d.clear();
      if(!Storage.exists(candidate)) return ReadResult::Invalid;
      if(maxBytes) {
        auto probe=Storage.open(candidate);
        if(!probe) return ReadResult::Unavailable;
        const size_t size=probe.size();
        const bool closed=probe.close();
        if(!closed || size>maxBytes) return ReadResult::Unavailable;
      }
      const auto json=Storage.readFile(candidate);
      if(json.empty()) {
        auto probe=Storage.open(candidate);
        if(!probe) return ReadResult::Unavailable;
        const bool emptyFile=probe.size()==0;
        return probe.close() && emptyFile ? ReadResult::Invalid : ReadResult::Unavailable;
      }
      ++io.parses;
      const auto error=deserializeJson(d,json);
      if(error==DeserializationError::NoMemory || d.overflowed()) return ReadResult::Unavailable;
      return error ? ReadResult::Invalid : ReadResult::Ready;
    };
    const auto main=parse(p);
    if(main!=ReadResult::Invalid) return main;
    const std::string backup=std::string(p)+".davbak";
    const auto recovery=parse(backup.c_str());
    if(recovery!=ReadResult::Ready) { d.clear();return recovery; }
    if(Storage.exists(p) && !Storage.remove(p)) return ReadResult::Ready;
    Storage.rename(backup.c_str(),p);
    return ReadResult::Ready;
  }

  static bool writeDocToFile(const char* p,const JsonDocument& d) {
    Storage.mkdir("/.crosspoint"); std::string s; serializeJson(d,s); return Storage.writeFile(p,s);
  }
};
template <class T> class PersistableStore: public PersistableStoreBase {
 public: static T& getInstance(){static T x; return x;}
};
