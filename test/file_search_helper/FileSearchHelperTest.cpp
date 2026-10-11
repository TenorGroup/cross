#include "FileSearch.h"

#include <iostream>
#include <string>
#include <vector>

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
  std::cout << "file search helper: PASS\n";
}
