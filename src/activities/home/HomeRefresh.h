#pragma once

#include <cstdint>

class HomeRefresh {
 public:
  static constexpr unsigned RESTORE_AFTER = 10;
  static constexpr uint32_t QUIET_MS = 2000;
  enum class Mode : uint8_t { Normal, Window, Restore };
  struct Plan { bool oneBit; Mode mode; bool changedBook; };

  Plan choose(bool recent, int book, uint16_t surface) const {
    const bool same = recent && book >= 0 && paintedBook >= 0 && surface == paintedSurface;
    const bool changed = same && book != paintedBook;
    return {recent, !recent && swaps ? Mode::Restore : changed ? Mode::Window : Mode::Normal, changed};
  }

  void painted(bool recent, int book, uint16_t surface, uint32_t now = 0) {
    if (choose(recent, book, surface).changedBook && swaps < UINT16_MAX) ++swaps;
    paintedBook = recent ? book : -1;
    paintedSurface = surface;
    quietSince = now;
  }

  bool idle(uint32_t now, bool front, bool input) {
    if (!front || input) quietSince = now;
    return front && !input && swaps >= RESTORE_AFTER && uint32_t(now - quietSince) >= QUIET_MS;
  }

  unsigned count() const { return swaps; }
  void restored() { swaps = 0; }
  void leave() { paintedBook = -1; }

 private:
  int paintedBook = -1;
  uint32_t quietSince = 0;
  uint16_t paintedSurface = 0;
  uint16_t swaps = 0;
};
