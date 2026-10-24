#pragma once
#include <string>
// Not a hash: the test compares what is sent, and both trees send the same stand-in.
class MD5Builder {
 public:
  void begin() { text_.clear(); }
  void add(const char* s) { text_ += s; }
  void calculate() {}
  std::string toString() const { return "md5(" + text_ + ")"; }

 private:
  std::string text_;
};
