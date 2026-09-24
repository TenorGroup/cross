#pragma once
#include <string>
namespace FsHelpers {
inline std::string normalisePath(const std::string& s) { return s; }
inline std::string decodeUriEscapes(const std::string& s) { return s; }
}  // namespace FsHelpers
