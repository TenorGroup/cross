#pragma once

#include <string>
#include <string_view>

namespace FsHelpers {
inline std::string decodeUriEscapes(const std::string& path) { return path; }
inline std::string normalisePath(const std::string& path) { return path; }
// What the TXT converter asks of the name: its extension (".md" is Markdown) and the name without it.
inline bool hasMarkdownExtension(std::string_view name) {
  return name.size() >= 3 && (name.substr(name.size() - 3) == ".md" || name.substr(name.size() - 3) == ".MD");
}
inline std::string getFileNameWithoutExtension(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
  const size_t dot = path.find_last_of('.');
  return std::string(dot == std::string_view::npos ? path : path.substr(0, dot));
}
}  // namespace FsHelpers
