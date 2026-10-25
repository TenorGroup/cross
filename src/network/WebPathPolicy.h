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
// Dot-named items are ordinary data (they can be listed and deleted from the file manager, #4031), except the
// root /.crosspoint tree: it holds every credential store (wifi, opds, opds tokens, koreader, webdav, lcp), not
// only the three files upstream's ProtectedPaths.h names.
constexpr bool allowed(std::string_view path) {
  if (path.empty()) return false;
  size_t start = 0;
  bool firstPart = true;
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
    // "." and ".." end in a dot, so the trailing-dot test below also refuses them.
    if (!part.empty() && (part.back() == '.' || part.back() == ' ' || equalsFolded(part, "XTCache") ||
                          equalsFolded(part, "System Volume Information") ||
                          (firstPart && equalsFolded(part, ".crosspoint")))) {
      return false;
    }
    if (!part.empty()) firstPart = false;
    start = i + 1;
  }
  return true;
}

// WebDAV's rule: web_path::allowed, and no dot-named item either (as in v1.0.76: clients such as Finder write
// .DS_Store and ._ files that would otherwise litter the card), and no credential store.
inline bool davAllowed(std::string_view path) {
  for (size_t start = 0; start < path.size();) {
    if (path[start] == '/') {
      ++start;
    } else {
      if (path[start] == '.') return false;
      const size_t slash = path.find('/', start);
      if (slash == std::string_view::npos) break;
      start = slash + 1;
    }
  }
  return allowed(path);
}
}  // namespace web_path
