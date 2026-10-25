#pragma once

#include <string.h>
#include <strings.h>

#include <string>

// Credential stores no web request (file manager, plugin endpoints) or plugin
// manifest may read or write: their passwords are obfuscated with a device
// key, so the files must stay on the device. Mirrors the getFilePath() of
// WifiCredentialStore, OpdsServerStore and KOReaderCredentialStore. Prefix
// match, so the stores' temp/backup siblings are covered too.
namespace protectedpaths {

inline constexpr const char* SENSITIVE_FILES[] = {"/.crosspoint/wifi.json", "/.crosspoint/opds.json",
                                                  "/.crosspoint/koreader.json"};

// SdFat also opens a name by its generated 8.3 alias (".crosspoint" is
// CROSSP~1), which no spelling check can map back to the long name.
inline bool isShortAlias(const char* first, const char* last) {
  const char* tilde = static_cast<const char*>(memchr(first, '~', static_cast<size_t>(last - first)));
  if (!tilde || tilde + 1 >= last || tilde[1] < '0' || tilde[1] > '9') return false;
  const char* dot = static_cast<const char*>(memchr(first, '.', static_cast<size_t>(last - first)));
  const size_t base = static_cast<size_t>((dot ? dot : last) - first);
  const size_t ext = dot ? static_cast<size_t>(last - dot - 1) : 0;
  return base <= 8 && ext <= 3;
}

// The path as SdFat would open it: empty components and "." dropped, ".." applied, leading spaces and trailing dots
// and spaces trimmed from every name. False when a name (two levels down at most) is an 8.3 alias of a long name, which
// no spelling check can map back.
inline bool canonicalPath(const char* path, std::string& canon) {
  canon.clear();
  canon.reserve(strlen(path) + 1);
  int depth = 0;
  for (const char* seg = path; *seg;) {
    while (*seg == '/') seg++;
    const char* end = strchr(seg, '/');
    const size_t len = end ? static_cast<size_t>(end - seg) : strlen(seg);
    if (len == 2 && seg[0] == '.' && seg[1] == '.') {
      const size_t slash = canon.rfind('/');
      canon.erase(slash == std::string::npos ? 0 : slash);
      if (depth > 0) depth--;
    } else {
      // SdFat skips leading spaces and drops trailing dots and spaces when it
      // opens a name, so compare the name it would actually open.
      const char* first = seg;
      const char* last = seg + len;
      while (first < last && *first == ' ') first++;
      while (last > first && (last[-1] == '.' || last[-1] == ' ')) last--;
      // The stores sit two levels deep; an alias further down cannot reach one.
      if (depth < 2 && isShortAlias(first, last)) return false;
      if (first < last) {
        canon += '/';
        canon.append(first, static_cast<size_t>(last - first));
        depth++;
      }
    }
    seg += len;
  }
  return true;
}

inline bool isSensitivePath(const char* path) {
  // Canonicalise first ("//", "/./" and ".." would otherwise dodge the prefix
  // match while still opening the same file).
  std::string canon;
  if (!canonicalPath(path, canon)) return true;
  for (const char* file : SENSITIVE_FILES) {
    // FAT names are case-insensitive.
    if (strncasecmp(canon.c_str(), file, strlen(file)) == 0) return true;
  }
  return false;
}

// Inside the device's own tree, /.crosspoint, a plugin has /.crosspoint/plugins/<name>/ and no more: the settings, the
// WebDAV login, the OPDS tokens and the rest are not its to write, as the file manager keeps the whole tree out of
// reach (WebPathPolicy.h). Everything outside the tree is the user's, and a plugin may name it.
inline bool isInPluginArea(const char* path) {
  std::string canon;
  if (!canonicalPath(path, canon)) return false;
  static constexpr char ROOT[] = "/.crosspoint";
  constexpr size_t rootLen = sizeof(ROOT) - 1;
  if (strncasecmp(canon.c_str(), ROOT, rootLen) != 0 || (canon.size() > rootLen && canon[rootLen] != '/')) return true;
  // /.crosspoint/plugins/<name>/<something>
  static constexpr char PLUGINS[] = "/plugins/";
  constexpr size_t pluginsLen = sizeof(PLUGINS) - 1;
  if (strncasecmp(canon.c_str() + rootLen, PLUGINS, pluginsLen) != 0) return false;
  const size_t name = rootLen + pluginsLen;
  const size_t slash = canon.find('/', name);
  return slash != std::string::npos && slash > name && slash + 1 < canon.size();
}

// A plugin-supplied path (web request or device.json): absolute, no parent
// refs, not a credential store, and inside the plugin area (isInPluginArea).
inline bool isPluginPath(const std::string& p) {
  return p.size() > 1 && p[0] == '/' && p.find("..") == std::string::npos && !isSensitivePath(p.c_str()) &&
         isInPluginArea(p.c_str());
}

}  // namespace protectedpaths
