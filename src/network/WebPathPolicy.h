#pragma once

#include <string_view>

namespace web_path {
constexpr char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

constexpr bool equalsFolded(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
  }
  return true;
}

// Apply to the decoded path before normalisation or any C-string conversion.
// FAT aliases and trailing dots/spaces must not bypass protected ancestors.
constexpr bool allowed(std::string_view path) {
  if (path.empty()) return false;
  size_t start = 0;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i < path.size() && path[i] != '/') {
      const auto c = static_cast<unsigned char>(path[i]);
      if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' ||
          c == '*')
        return false;
      continue;
    }
    const auto part = path.substr(start, i - start);
    const auto tilde = part.find('~');
    const auto dot = part.find('.');
    const auto base = part.substr(0, dot);
    bool shortAlias = tilde != std::string_view::npos && tilde > 0 && tilde + 1 < base.size() && base.size() <= 8;
    if (shortAlias) {
      for (size_t j = tilde + 1; j < base.size(); ++j)
        if (base[j] < '0' || base[j] > '9') shortAlias = false;
    }
    if (shortAlias || (part.size() >= 7 && (equalsFolded(part.substr(part.size() - 7), ".davtmp") ||
                                            equalsFolded(part.substr(part.size() - 7), ".davbak"))))
      return false;
    if (!part.empty() && (part.front() == '.' || part.back() == '.' || part.back() == ' ' ||
                          equalsFolded(part, "XTCache") || equalsFolded(part, "System Volume Information"))) {
      return false;
    }
    start = i + 1;
  }
  return true;
}
}  // namespace web_path
