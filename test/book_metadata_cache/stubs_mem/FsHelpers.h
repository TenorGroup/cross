#pragma once
#include <string>
namespace FsHelpers {
inline std::string normalisePath(const std::string& s) { return s; }
// The test books are EPUB archives; TXT and Markdown take another size path.
inline bool hasTxtExtension(const std::string&) { return false; }
inline bool hasMarkdownExtension(const std::string&) { return false; }
}  // namespace FsHelpers
