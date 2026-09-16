#pragma once
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
  bool operator==(const String& o) const { return s_ == o.s_; }
  String operator+(const String& o) const { return String(s_ + o.s_); }
 private:
  std::string s_;
};
class __FlashStringHelper;
#ifndef F
#define F(x) (x)
#endif
