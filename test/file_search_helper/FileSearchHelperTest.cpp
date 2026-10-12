#include "FileSearch.h"

#include <iostream>
#include <string>
#include <vector>

static bool refuseArray = false;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (refuseArray) return nullptr;
  try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept { ::operator delete[](memory); }

namespace {
void require(const bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}

int main() {
  require(filesearch::matchRank("Duong Sach.epub", "duong") == 0, "whole query at stem start");
  require(filesearch::matchRank("Sach Duong.epub", "duong") == 1, "word prefix");
  require(filesearch::matchRank("DaiDuong.epub", "uong") == 2, "middle substring");
  require(filesearch::matchRank("Duong.epub", "duong sach") == 255, "all words required");
  require(filesearch::matchRank("Đường.epub", "DƯỜNG") == 0, "NFC and NFD fold");

  const std::vector<std::string> names = {"10 sach.epub", "2 sach.epub", "Sach Duong.epub", "DaiDuong.epub"};
  filesearch::IndexBuffer results;
  require(filesearch::buildResults(names, "sach", results), "build results");
  require(results.size() == 3, "result count");
  require(names[results[0]] == "Sach Duong.epub" && names[results[1]] == "2 sach.epub" &&
              names[results[2]] == "10 sach.epub",
          "rank then natural order");
  require(filesearch::matchRank("ĐạiDuong xaSach.epub", "uong sach") == 2, "separate middle tokens");
  require(filesearch::matchRank("Duong va Sach.epub", "sach duong") == 1, "reordered prefixes");
  require(filesearch::matchRank("ĐƯỜNG.epub", "đường") == 0, "uppercase Vietnamese");
  require(filesearch::matchRank("Đường.epub", "epub") == 255, "extension excluded");
  require(filesearch::matchRank("/Duong/Sach.epub", "duong") == 255, "parent excluded");
  require(filesearch::matchRank("Duong/", "duong") == 0, "folder stem");
  require(filesearch::buildResults(names, "", results), "clear query");
  require(results.size() == names.size(), "all original entries restored");
  for (size_t i = 0; i < names.size(); ++i) require(results[i] == i, "original order restored");
  require(!results.allocate(65536, "test") && results.size() == 0, "index overflow refuses cleanly");
  require(filesearch::nameLess("/z/Book 2.epub", "/a/Book 10.epub"), "natural basename order");
  require(filesearch::nameLess("Đường 2.epub", "Đường xa.epub"), "folded natural order across NFC and NFD");
  require(filesearch::nameLess("/a/Book.epub", "/b/Book.epub"), "raw path tie");
  refuseArray = true;
  require(!filesearch::buildResults(names, "sach", results) && results.size() == 0 && results.capacity() == 0,
          "index OOM clears result without changing source");
  refuseArray = false;
  require(names[0] == "10 sach.epub" && filesearch::buildResults(names, "sach", results) && results.size() == 3,
          "index allocation recovers after refusal");
  std::cout << "file search helper: PASS\n";
}
