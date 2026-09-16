#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct QuoteRecord {
  std::string path, title, text;
  int spine = 0, page = 0;
  uint32_t day = 0;
};
namespace quotes {
constexpr size_t MAX_BYTES = 1024;
constexpr size_t PAGE_SIZE = 20;
bool save(const QuoteRecord& quote);
bool load(const std::string& name, QuoteRecord& quote);
void list(const std::string& boundary, bool previous, std::vector<std::string>& names);
}  // namespace quotes
