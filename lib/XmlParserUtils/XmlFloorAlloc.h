#pragma once

// expat's allocator through the heap floor door (HeapFloor.h). A block of 4 KB or more that would
// leave the heap under the floor is refused: expat stops with a memory error, the door counts the
// refusal, and the caller tells it from a broken file. Each block carries its size in front so a
// realloc can go through the door as a new block.
#include <HeapFloor.h>
#include <expat.h>

#include <cstddef>
#include <cstring>
#include <new>

namespace xmlfloor {
constexpr size_t kHead = alignof(std::max_align_t);

inline void* take(const size_t bytes) {
  auto* base = static_cast<unsigned char*>(::operator new(bytes + kHead, std::nothrow));
  if (!base) return nullptr;
  if (!heapfloor::judge(bytes, base, "expat")) {
    ::operator delete(base);
    return nullptr;
  }
  std::memcpy(base, &bytes, sizeof bytes);
  return base + kHead;
}

inline void give(void* block) {
  if (block) ::operator delete(static_cast<unsigned char*>(block) - kHead);
}

inline void* retake(void* block, const size_t bytes) {
  if (!block) return take(bytes);
  size_t had;
  std::memcpy(&had, static_cast<unsigned char*>(block) - kHead, sizeof had);
  void* moved = take(bytes);
  if (!moved) return nullptr;  // expat keeps the old block
  std::memcpy(moved, block, had < bytes ? had : bytes);
  give(block);
  return moved;
}

inline constexpr XML_Memory_Handling_Suite kSuite{take, retake, give};

// XML_ParserCreate(nullptr) with every block through the door.
inline XML_Parser create() { return XML_ParserCreate_MM(nullptr, &kSuite, nullptr); }
}  // namespace xmlfloor
