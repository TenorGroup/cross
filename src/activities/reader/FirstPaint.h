#pragma once
#include <cstdint>

// How a book is opened, for the refresh of the reader's first paint.
enum class ReaderOpen : uint8_t {
  FromMenu,  // a book chosen on Home, in File, in Favorites, or by the probe's READ_RECENT and OPEN_BOOK
  Other,     // a wake straight into the book, a restart, another book from the end of one
};

// The one rule for the first paint, asked by every caller. The X4 Pro opens a book chosen on a menu with the
// fast refresh (485 ms) instead of the cleaning one (1334 ms); the cleaning cycle of the pages after it
// is unchanged. A wake keeps the cleaning pass, which takes the sleep image off the panel. The X3 and
// the X4 keep their policy.
constexpr bool fastFirstPaint(const bool x4pro, const ReaderOpen open) { return x4pro && open == ReaderOpen::FromMenu; }
