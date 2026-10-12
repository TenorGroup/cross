"""Exercise stylesheet extraction through the production EPUB parse method."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'lib/Epub/Epub.cpp'
text = path.read_text()
start = text.index('CssParser::ParseResult Epub::parseCssFiles(')
end = text.index('\n// load in the meta data', start)
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
template<class... T> void logLine(T&&...) {}
#define LOG_DBG(...) logLine(__VA_ARGS__)
#define LOG_ERR(...) logLine(__VA_ARGS__)
bool refuse = false;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t n) {
  using E = std::remove_extent_t<T>;
  return refuse ? nullptr : std::unique_ptr<T>(new E[n]());
}
namespace heapfloor {
template<class T> auto array(size_t n, const char*) { return makeUniqueNoThrow<T>(n); }
}
struct { unsigned getFreeHeap() { return 131072; } } ESP;
struct HalFile { void close() {} };
struct {
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return true; }
  bool openFileForRead(const char*, const std::string&, HalFile&) { return true; }
  void remove(const char*) {}
} Storage;
class CssParser {
 public:
  enum class CacheStatus { Missing, Complete, Partial, Invalid };
  enum class ParseResult { Complete, Partial, Error };
  void clear() {}
  bool empty() const { return false; }
  size_t ruleCount() const { return 2; }
  bool saveToCache(bool) { return true; }
  ParseResult loadFromStream(HalFile&) { return ParseResult::Complete; }
};
namespace FsHelpers { bool hasCssExtension(std::string_view s) { return s.ends_with(".css"); } }
class ZipFile {
 public:
  explicit ZipFile(const std::string&) {}
  static uint64_t fnvHash64(const char* s, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for(size_t i = 0; i < n; ++i) h = (h ^ static_cast<unsigned char>(s[i])) * 1099511628211ull;
    return h;
  }
  template<class F> void enumerateFileEntries(F fn) {
    fn("first.css", 0x12345678u, 80u);
    fn("second.css", 0x12345678u, 80u);
  }
};
class Epub {
 public:
  std::vector<std::string> cssFiles;
  mutable std::vector<std::string> extracted;
  std::string filepath;
  std::unique_ptr<CssParser> cssParser = std::make_unique<CssParser>();
  std::string getCachePath() const { return "/cache"; }
  bool getItemSize(const std::string&, size_t* n) const { *n = 100; return true; }
  bool readItemContentsToStream(const std::string& path, HalFile&, size_t) const {
    extracted.push_back(path); return true;
  }
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus) const;
};
'''
source += text[start:end]
source += r'''
int main() {
  Epub different;
  different.cssFiles = {"first.css", "second.css"};
  assert(different.parseCssFiles(CssParser::CacheStatus::Missing) == CssParser::ParseResult::Complete);
  assert(different.extracted == different.cssFiles);
  Epub repeated;
  repeated.cssFiles = {"first.css", "first.css"};
  assert(repeated.parseCssFiles(CssParser::CacheStatus::Missing) == CssParser::ParseResult::Complete);
  assert(repeated.extracted == std::vector<std::string>{"first.css"});
  refuse = true;
  Epub fallback;
  fallback.cssFiles = {"first.css", "second.css"};
  assert(fallback.parseCssFiles(CssParser::CacheStatus::Missing) == CssParser::ParseResult::Complete);
  assert(fallback.extracted == fallback.cssFiles);
  puts("CRC and size collision: 2 files; repeated name: 1 file; heap refusal: 2 files");
}
'''
with tempfile.TemporaryDirectory(prefix='epub-css-') as folder:
    cpp, binary = Path(folder) / 'dedup.cpp', Path(folder) / 'dedup'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
