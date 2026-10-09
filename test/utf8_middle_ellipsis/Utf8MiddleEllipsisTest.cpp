#include "Utf8.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

bool countAllocations = false;
size_t allocationCount = 0;

void* allocate(const size_t size) {
  if (countAllocations) ++allocationCount;
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}

std::string referenceMiddleEllipsis(const std::string& input, const int maxWidth) {
  const auto measure = [](const char* text) { return static_cast<int>(std::strlen(text) * 8); };
  if (input.empty() || maxWidth <= 0) return {};
  if (measure(input.c_str()) <= maxWidth) return input;

  constexpr const char* ellipsis = "\xE2\x80\xA6";
  if (measure(ellipsis) > maxWidth) return {};

  const auto* begin = reinterpret_cast<const unsigned char*>(input.c_str());
  const auto* cursor = begin;
  std::vector<size_t> offsets;
  offsets.reserve(input.size() + 1);
  while (*cursor) {
    offsets.push_back(static_cast<size_t>(cursor - begin));
    utf8NextCodepoint(&cursor);
  }
  offsets.push_back(input.size());
  const int chars = static_cast<int>(offsets.size()) - 1;
  if (chars < 3) return ellipsis;

  const int minHead = chars >= 6 ? 2 : 1;
  const int minTail = chars >= 6 ? 2 : 1;
  for (int kept = chars - 1; kept >= minHead + minTail; --kept) {
    const int headChars = std::clamp((kept * 2 + 4) / 5, minHead, kept - minTail);
    const int tailChars = kept - headChars;
    std::string candidate = input.substr(0, offsets[headChars]);
    candidate += ellipsis;
    candidate += input.substr(offsets[chars - tailChars]);
    if (measure(candidate.c_str()) <= maxWidth) return candidate;
  }
  return ellipsis;
}

}  // namespace

void* operator new(const std::size_t size) { return allocate(size); }
void* operator new[](const std::size_t size) { return allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, const std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::size_t) noexcept { std::free(memory); }

int main() {
  const std::string cases[] = {
      "abcdef",
      "CrossPoint reader menu",
      "Tiếng Việt có dấu",
      "Đọc chương 18 - Cài đặt",
      "SP3 - Traveling Typewriter-BOLD1",
      "SP3 - Traveling Typewriter-BOLD2",
      "a",
      "ab",
      "abc",
      "Đỗ",
  };
  const auto measure = [](const char* text) { return static_cast<int>(std::strlen(text) * 8); };

  for (const auto& input : cases) {
    assert(utf8MiddleEllipsis(input, 48, measure) == referenceMiddleEllipsis(input, 48));
  }

  for (const auto& input : cases) {
    size_t measureCalls = 0;
    size_t allocationsAfterReserve = 0;
    allocationCount = 0;
    countAllocations = true;
    const auto result = utf8MiddleEllipsis(input, 48, [&](const char* text) {
      ++measureCalls;
      if (measureCalls == 3) allocationsAfterReserve = allocationCount;
      if (measureCalls >= 3) assert(allocationCount == allocationsAfterReserve);
      return measure(text);
    });
    countAllocations = false;
    assert(!result.empty());
    assert(allocationCount <= 1);
    if (measureCalls >= 3) assert(allocationCount == allocationsAfterReserve);
  }
  const std::string longInput(512, 'a');
  size_t measureCalls = 0;
  allocationCount = 0;
  countAllocations = true;
  const auto longResult = utf8MiddleEllipsis(longInput, 48, [&](const char* text) {
    ++measureCalls;
    assert(allocationCount == (measureCalls >= 3 ? 1 : 0));
    return measure(text);
  });
  countAllocations = false;
  assert(allocationCount == 1);
  assert(measureCalls > 3);
  assert(longResult == referenceMiddleEllipsis(longInput, 48));
  std::puts("GREEN UTF-8 middle ellipsis: 10 legacy cases + 512-byte probe, 0 loop allocations after reserve");
}
