#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#ifndef TENOR_HOST_STRING_DEFINED
#define TENOR_HOST_STRING_DEFINED
class String {
 public:
  String() = default;
  String(const char* text) : value_(text ? text : "") {}
  String(const std::string& text) : value_(text) {}
  const char* c_str() const { return value_.c_str(); }
  int read() const { return readPos_ < value_.size() ? static_cast<unsigned char>(value_[readPos_++]) : -1; }
  size_t write(uint8_t value) { value_.push_back(static_cast<char>(value)); return 1; }
  size_t write(const uint8_t* data, size_t count) {
    value_.append(reinterpret_cast<const char*>(data), count);
    return count;
  }
  bool operator==(const String& other) const { return value_ == other.value_; }
 private:
  std::string value_;
  mutable size_t readPos_ = 0;
};
#endif
struct MemoryFile {
  bool valid; size_t bytes;
  explicit operator bool() const { return valid; }
  size_t size() const { return bytes; }
  void close() {}
};
struct MemoryStorage {
  std::map<std::string,std::string> files;
  bool writes=true;
  MemoryFile open(const char* path) { return {files.count(path)>0,files.count(path)?files.at(path).size():0}; }
  String readFile(const char* path) { return files.count(path)?String(files.at(path)):String(); }
  bool writeFile(const char* path, const String& text) { if(!writes)return false; files[path]=text.c_str(); return true; }
  void mkdir(const char*) {}
  bool exists(const char* path) { return files.count(path); }
  bool remove(const char* path) { return files.erase(path)>0; }
  bool rename(const char* from,const char* to) { if(!files.count(from))return false; files[to]=files.at(from); files.erase(from); return true; }
};
inline MemoryStorage Storage;
