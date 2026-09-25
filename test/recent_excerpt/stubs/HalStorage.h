#pragma once
struct HalStorageFake {
  bool exists(const char*) const { return true; }
};
inline HalStorageFake Storage;
