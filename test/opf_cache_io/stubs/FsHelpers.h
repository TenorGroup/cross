#pragma once
#include <string>
namespace FsHelpers {
inline std::string normalisePath(const std::string& value) { return value; }
inline std::string decodeUriEscapes(const std::string& value) { return value; }
}
