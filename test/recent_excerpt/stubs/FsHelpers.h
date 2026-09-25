#pragma once
#include <string>
namespace FsHelpers {
inline bool endsWith(const std::string& s, const char* e) {
  const std::string x(e);
  return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
inline bool hasEpubExtension(const std::string& s) { return endsWith(s, ".epub"); }
inline bool hasXtcExtension(const std::string& s) { return endsWith(s, ".xtc") || endsWith(s, ".xtch"); }
inline bool hasTxtExtension(const std::string& s) { return endsWith(s, ".txt"); }
inline bool hasMarkdownExtension(const std::string& s) { return endsWith(s, ".md"); }
}  // namespace FsHelpers
