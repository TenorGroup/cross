#pragma once

// A card that stops answering while a book is open (X3, 18/10/2026: every command timed out
// until the device was restarted). The reader restarts once into the same book instead of
// showing the page load error; the boot's SD begin starts the card again, as the user's own
// restart did. One rescue per boot chain, so a card that stays dead ends on the SD error screen.

#include <cstdint>

namespace cardrescue {

// True once this boot chain has restarted for the card.
inline bool rescued = false;

// The book file answers: "PK" at its start and a byte from its second sector. SdFat caches one
// sector, so at least one of the two reads reaches the card.
template <typename File>
bool answers(File& f) {
  uint8_t b[2];
  return f.read(b, 2) == 2 && b[0] == 'P' && b[1] == 'K' && f.seekSet(512) && f.read(b, 1) == 1;
}

// Restart for the card? Only on the X3, only once per chain, and only when the book file stops
// answering. The probe (open the book, then answers()) runs last: on a dead card it takes seconds.
template <typename Probe>
bool shouldRestart(const bool x3, Probe&& bookAnswers) {
  return x3 && !rescued && !bookAnswers();
}

}  // namespace cardrescue
