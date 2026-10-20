#pragma once

// The font system includes HalStorage for the vector (.ttf) path only; that
// path is compiled out without PSRAM. begin() asks whether a card was read before it forgets a family.
struct HostStorage {
  bool cardRead = true;
  bool ready() const { return cardRead; }
};
inline HostStorage Storage;
