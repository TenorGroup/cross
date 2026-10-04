#pragma once

// Settle timer for the Text panel's in-place rows (size, line spacing, alignment, drop cap). Each
// press changes the value on the row at once; the chapter is laid out again only once the presses
// have stopped for kQuietMs, so 4 quick presses cost 1 layout instead of 4.
struct TextRelayoutQuiet {
  static constexpr unsigned long kQuietMs = 600;  // pause after the last press (ms)

  void touch(const unsigned long now) {
    dueAt = now + kQuietMs;
    pending = true;
  }
  bool isPending() const { return pending; }
  // The quiet time has passed since the last press. Wrap-safe for a millis() counter.
  bool due(const unsigned long now) const { return pending && static_cast<long>(now - dueAt) >= 0; }
  void clear() { pending = false; }

 private:
  unsigned long dueAt = 0;
  bool pending = false;
};
