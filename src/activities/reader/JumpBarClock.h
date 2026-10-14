#pragma once

#include <cstdint>

// The way back's bar after a jump counts 3, 2, 1. Each number stands on the panel DWELL_MS from the moment the refresh
// that showed it has finished, however long that refresh took (a ring refresh the driver turns into a full one takes
// seconds), so no refresh eats into a number's time and the bar stands 3 x DWELL_MS at least. The bar belongs to a page:
// a repaint of that same page (a background step that lent the framebuffer, a status note) carries it on with the number
// it had; a paint of any other page takes it down.
namespace jumpbar {

constexpr unsigned long DWELL_MS = 1000;
constexpr int START = 3;

enum class Due : uint8_t { Wait, Next, Hide };

struct Clock {
  int digit = 0;               // the number on the bar, 0 = no bar
  unsigned long shownAt = 0;   // when the refresh that put `digit` on the panel finished, 0 = it has not yet
  int spine = -1, page = -1;   // the page the bar stands on

  bool up() const { return digit > 0; }
  void arm(const int onSpine, const int onPage) {
    digit = START;
    shownAt = 0;
    spine = onSpine;
    page = onPage;
  }
  void drop() {
    digit = 0;
    shownAt = 0;
  }
  bool stands(const int onSpine, const int onPage) const { return up() && onSpine == spine && onPage == page; }
  // The next frame sent to the panel carries `digit` again: it counts from when that frame has finished.
  void repainting() { shownAt = 0; }
  void onPanel(const unsigned long now) { shownAt = now; }
  // What the bar needs now: nothing yet, the next number lower, or to go.
  Due due(const unsigned long now) const {
    if (!up() || shownAt == 0 || now - shownAt < DWELL_MS) return Due::Wait;
    return digit > 1 ? Due::Next : Due::Hide;
  }
  void next() { --digit; }
};

}  // namespace jumpbar
