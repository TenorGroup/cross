#pragma once

#include <cstdint>

class HomeRefresh {
 public:
  enum class Mode : uint8_t { Normal, Window };
  struct Plan { bool oneBit; Mode mode; bool changedBook; };

  Plan choose(bool recent, int book, uint16_t surface) const {
    const bool same = recent && book >= 0 && paintedBook >= 0 && surface == paintedSurface;
    const bool changed = same && book != paintedBook;
    return {recent, changed ? Mode::Window : Mode::Normal, changed};
  }

  void painted(bool recent, int book, uint16_t surface) {
    paintedBook = recent ? book : -1;
    paintedSurface = surface;
  }

  void leave() { paintedBook = -1; }

 private:
  int paintedBook = -1;
  uint16_t paintedSurface = 0;
};
