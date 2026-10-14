#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ProgressComparison.h"

// Every decision of the automatic KOReader sync lives here, so the sleep push and the wake pull ask
// the same functions (one decision, one function) and the host tests run the production code.
namespace kosyncauto {

// One automatic round (Wi-Fi join, TLS and every request) gets this long, then it gives up and the
// book stays as it is. X3 measurements of 14/10/2026: join 7.1 s through the manual screen (scan
// included), GET 2.1 s, PUT 2.0 s. One pushed book is join + GET + PUT = 11.2 s, so 12 s left no
// margin; 15 s keeps about 25% over that. The ceiling is only felt at sleep, behind a finished sleep screen.
constexpr uint32_t ROUND_BUDGET_MS = 15000;
// A request starts only when this much of the round is left (measured 2.1 s and 2.0 s, plus 50%).
constexpr uint32_t REQUEST_MS = 3000;

// What the TLS handshake needs (the same floor the manual sync checks).
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

// The wake pull runs only when the machine slept at least this long (founder, 14/10/2026): a shorter
// sleep opens the book at once, as before.
constexpr uint32_t PULL_MIN_SLEEP_SECONDS = 30 * 60;

// A join that starts from the remembered channel and access point gets this long before it is given
// up for a normal join (which scans). The measurement to set it from is in the lane notes: run the sleep
// push with and without kosync-wifi.json and compare the "join ... ms" log lines.
constexpr uint32_t FAST_JOIN_MS = 4000;

// Books owed a push at once; the oldest falls out when a fifth arrives.
constexpr size_t MAX_DEBTS = 4;

enum class Eligibility : uint8_t { Ready, Disabled, NoAccount, NoNetwork, RadioBusy, LowHeap };

// May an automatic round run now? Off by default: Disabled means nothing below happens at all.
constexpr Eligibility eligibility(const bool enabled, const bool hasAccount, const bool hasSavedNetwork,
                                  const size_t freeBytes, const size_t largestBlock, const bool radioFree) {
  if (!enabled) return Eligibility::Disabled;
  if (!hasAccount) return Eligibility::NoAccount;
  if (!hasSavedNetwork) return Eligibility::NoNetwork;
  if (!radioFree) return Eligibility::RadioBusy;
  if (freeBytes < MIN_FREE_FOR_TLS || largestBlock < MIN_BLOCK_FOR_TLS) return Eligibility::LowHeap;
  return Eligibility::Ready;
}

// Is there time left to begin one more request?
constexpr bool canStartRequest(const uint32_t elapsedMs) { return elapsedMs + REQUEST_MS <= ROUND_BUDGET_MS; }

// Has the machine slept long enough for a wake pull? Both stamps must come from a trusted clock:
// without one (or with a clock that went backwards) the sleep counts as short and nothing is pulled.
constexpr bool sleptLongEnough(const uint32_t sleptAt, const uint32_t now, const bool clockTrusted) {
  return clockTrusted && sleptAt != 0 && now >= sleptAt && now - sleptAt >= PULL_MIN_SLEEP_SECONDS;
}

// What the server said about a book.
enum class Fetch : uint8_t { Found, NotFound, Failed };

enum class PushAction : uint8_t { Push, DropDebt, KeepDebt };

// The comparison is local against server. A server copy that is ahead or equal needs no push (the
// wake pull brings it over); no copy at all is uploaded; an error keeps the debt for the next sleep.
constexpr PushAction pushAction(const Fetch fetch, const ProgressComparison comparison) {
  if (fetch == Fetch::Failed) return PushAction::KeepDebt;
  if (fetch == Fetch::NotFound) return PushAction::Push;
  return comparison == ProgressComparison::LocalAhead ? PushAction::Push : PushAction::DropDebt;
}

enum class PullAction : uint8_t { Apply, Keep };

// Only a server position that is ahead moves the book. Equal, behind, unknown or failed: keep the page.
constexpr PullAction pullAction(const Fetch fetch, const ProgressComparison comparison) {
  return fetch == Fetch::Found && comparison == ProgressComparison::RemoteAhead ? PullAction::Apply
                                                                                : PullAction::Keep;
}

// The reading place of a book as /.crosspoint/<book>/progress.bin keeps it: spine and page (2 bytes
// each, always), the chapter's page count (2 bytes, files of 6 and 10 bytes) and the visible text offset
// (4 bytes, files of 10 bytes), little endian. Same reading as EpubReaderActivity::loadBook. Any other
// length is not a progress file. 0xFFFF as the page is the old "last page" sentinel and means page 0.
struct SavedPlace {
  int spineIndex = 0;
  int pageNumber = 0;
  int totalPages = 0;  // 0 when the file does not say
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
};

inline std::optional<SavedPlace> parseProgressFile(const uint8_t* data, const size_t size) {
  if (size != 4 && size != 6 && size != 10) return std::nullopt;
  SavedPlace place;
  place.spineIndex = data[0] | (data[1] << 8);
  place.pageNumber = data[2] | (data[3] << 8);
  if (place.pageNumber == UINT16_MAX) place.pageNumber = 0;
  if (size >= 6) place.totalPages = data[4] | (data[5] << 8);
  if (size == 10) {
    place.hasVisibleTextOffset = true;
    place.visibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                              (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
  }
  return place;
}

}  // namespace kosyncauto
