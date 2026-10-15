#pragma once
#ifndef TENOR_HOST_STRING_DEFINED
#define TENOR_HOST_STRING_DEFINED
#include <cstddef>
#include <string>
// Arduino String la KIEU RIENG, khong phai bi danh cua std::string.
// Khai bi danh lam cac chong ham trong kho bi thu lai va dung nhau.
class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  int read() const { return readPos_ < s_.size() ? static_cast<unsigned char>(s_[readPos_++]) : -1; }
  size_t write(uint8_t value) { s_.push_back(static_cast<char>(value)); return 1; }
  size_t write(const uint8_t* data, size_t count) {
    s_.append(reinterpret_cast<const char*>(data), count);
    return count;
  }
  String& operator=(const std::string& value) { s_ = value; readPos_ = 0; return *this; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  String operator+(const String& o) const { return String(s_ + o.s_); }
 private:
  std::string s_;
  mutable size_t readPos_ = 0;
};
class __FlashStringHelper;
#ifndef F
#define F(x) (x)
#endif
#endif
