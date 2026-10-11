#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <HeapFloor.h>

namespace filesearch {

struct Match {
  uint16_t index = 0;
  uint8_t rank = 0;
};

class IndexBuffer {
 public:
  bool allocate(size_t capacity, const char* owner);
  void clear() { size_ = 0; }
  bool push(uint16_t index) {
    if (!data_ || size_ >= capacity_) return false;
    data_[size_++] = index;
    return true;
  }
  uint16_t operator[](size_t index) const { return data_[index]; }
  uint16_t* data() { return data_.get(); }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }

 private:
  std::unique_ptr<uint16_t[]> data_;
  size_t capacity_ = 0;
  size_t size_ = 0;
};

// Returns 0 for the whole folded query at the start of the stem, 1 when every
// folded query word starts a word, 2 when every word occurs anywhere, and 255
// when one word is absent. The file extension is excluded.
uint8_t matchRank(const std::string& entry, const std::string& query);

// Builds a stable result index. The source remains owned by the caller, so the
// result buffer contains only uint16 source indexes.
bool buildResults(const std::vector<std::string>& source, const std::string& query, IndexBuffer& result,
                  const char* owner = "file-search-results");

}  // namespace filesearch
