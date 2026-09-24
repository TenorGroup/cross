#pragma once
// Heap model for the huge-book tests. The firmware builds with exceptions off,
// so a throwing operator new that runs out of heap aborts and reboots the
// device. Here every tracked allocation is counted against a byte cap: a
// throwing allocation past the cap is recorded as an abort, a nothrow
// allocation past the cap returns nullptr the way the device heap does.
// Include from exactly one translation unit per test binary.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "HeapCapState.h"


void* operator new(std::size_t size) { return heapcap::allocate(size, false); }
void* operator new[](std::size_t size) { return heapcap::allocate(size, false); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return heapcap::allocate(size, true);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return heapcap::allocate(size, true);
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* p) noexcept { heapcap::release(p); }
void operator delete[](void* p) noexcept { heapcap::release(p); }
void operator delete(void* p, std::size_t) noexcept { heapcap::release(p); }
void operator delete[](void* p, std::size_t) noexcept { heapcap::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { heapcap::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heapcap::release(p); }
