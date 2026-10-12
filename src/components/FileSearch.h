#pragma once

#include <HeapFloor.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace filesearch {

std::string_view basename(std::string_view path);
std::string_view stem(std::string_view path);
std::string_view parent(std::string_view path);
// Natural basename order, then raw path bytes. Paths retain their filesystem spelling.
bool nameLess(std::string_view left, std::string_view right);

class Query {
 public:
  explicit Query(std::string_view text);
  uint8_t rank(std::string_view entry) const;
 private:
  std::string folded_;
};
uint8_t matchRank(const std::string& entry, const std::string& query);

class IndexBuffer {
 public:
  bool allocate(size_t capacity, const char* owner);
  void clear() { data_.reset(); capacity_ = size_ = 0; }
  bool push(uint16_t index) {
    if (size_ >= capacity_) return false;
    data_[size_++] = index;
    return true;
  }
  uint16_t operator[](size_t index) const { return data_[index]; }
  uint16_t* data() { return data_.get(); }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
 private:
  std::unique_ptr<uint16_t[]> data_;
  size_t capacity_ = 0, size_ = 0;
};
bool buildResults(const std::vector<std::string>& source, const std::string& query, IndexBuffer& result,
                  const char* owner = "file-search-results");
}  // namespace filesearch
