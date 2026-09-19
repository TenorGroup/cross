#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
class String {
 std::string text;
 public:
 static inline bool failConcat=false;
 String(const char* s=""):text(s?s:""){}
 String(const std::string& s):text(s){}
 const char* c_str()const{return text.c_str();}
 size_t length()const{return text.size();}
 bool isEmpty()const{return text.empty();}
 bool reserve(size_t){return !failConcat;}
 bool concat(const char*s,size_t n){if(failConcat)return false;text.append(s,n);return true;}
 String& operator+=(char c){text+=c;return *this;}
 bool concat(const char*s){if(failConcat)return false;text+=s;return true;}
 String& operator=(const char*s){text=s?s:"";return *this;}
};
struct SerialT {operator bool()const{return false;} void println(const char*){} template<class...A>void printf(const char*,A...){} }; inline SerialT Serial;
