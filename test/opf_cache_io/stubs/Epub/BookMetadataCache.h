#pragma once
#include <string>
#include <vector>
class BookMetadataCache {
 public:
  std::vector<std::string> entries;
  void createSpineEntry(const std::string& href) { entries.push_back(href); }
};
