#pragma once
#include <string>
namespace FsHelpers {
inline std::string normalisePath(const std::string& s) { return s; }
inline std::string decodeUriEscapes(const std::string& s) { return s; }
inline bool hasTxtExtension(const std::string&) { return false; }
inline bool hasMarkdownExtension(const std::string&) { return false; }
}  // namespace FsHelpers
