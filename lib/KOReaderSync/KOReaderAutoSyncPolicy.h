#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

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

// The timeout of one request: what is left of the round, never above the cap and never below REQUEST_MS.
// The Epub load and the mapping before an upload can use up the round; the PUT (2.0 s measured) must
// still have the time of a request, so the floor is REQUEST_MS. A round can overrun by at most that.
constexpr uint32_t REQUEST_TIMEOUT_MAX_MS = 6000;
constexpr uint32_t requestTimeoutMs(const uint32_t elapsedMs) {
  const int32_t left = static_cast<int32_t>(ROUND_BUDGET_MS) - static_cast<int32_t>(elapsedMs);
  const int32_t floor = static_cast<int32_t>(REQUEST_MS);
  const int32_t cap = static_cast<int32_t>(REQUEST_TIMEOUT_MAX_MS);
  return static_cast<uint32_t>(left < floor ? floor : left > cap ? cap : left);
}

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
// Books whose last synced record is remembered (KOReaderSyncMark.h); the oldest falls out first.
constexpr size_t MAX_MARKS = 8;

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
// `minSeconds` is the rule; only a measuring build passes another value than the 30 minutes.
constexpr bool sleptLongEnough(const uint32_t sleptAt, const uint32_t now, const bool clockTrusted,
                               const uint32_t minSeconds = PULL_MIN_SLEEP_SECONDS) {
  return clockTrusted && sleptAt != 0 && now >= sleptAt && now - sleptAt >= minSeconds;
}

// Only an Epub has a place the server's record maps onto; a text or XTC book is never pulled (the Wi-Fi
// wait would be spent for a book that cannot take the result).
inline bool isEpubPath(const std::string& path) {
  static constexpr char ext[] = ".epub";
  constexpr size_t n = sizeof(ext) - 1;
  if (path.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = path[path.size() - n + i];
    if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != ext[i]) return false;
  }
  return true;
}

// A debt is dropped as "the book is gone" only when the card answers (it holds the sync folder) and the
// book is not on it; a card that does not answer, or a book that is there but cannot be read, keeps the debt.
enum class BookCheck : uint8_t { Gone, Unreadable, Ready };
constexpr BookCheck checkBook(const bool cardAnswers, const bool bookThere, const bool hashed) {
  if (!cardAnswers) return BookCheck::Unreadable;
  if (!bookThere) return BookCheck::Gone;
  return hashed ? BookCheck::Ready : BookCheck::Unreadable;
}

// What the server said about a book.
enum class Fetch : uint8_t { Found, NotFound, Failed };

// What this machine last put on the server (a push) or took from it (a pull) for a book. Nothing in it
// depends on a clock: the server's record is compared by what it says, not by when it was written.
// recordToken: the record's progress string and percentage (to four digits) folded into one number.
// placeKey: this machine's spine and page at that moment.
inline uint32_t recordToken(const std::string& progress, const float percentage) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const char c : progress) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  const float p = percentage != percentage ? 0.0f : percentage < 0.0f ? 0.0f : percentage > 1.0f ? 1.0f : percentage;
  return (h ^ static_cast<uint32_t>(p * 10000.0f + 0.5f)) * 16777619u;
}
constexpr uint32_t placeKey(const int spineIndex, const int pageNumber) {
  return (static_cast<uint32_t>(spineIndex) & 0xFFFFu) << 16 | (static_cast<uint32_t>(pageNumber) & 0xFFFFu);
}

enum class PushAction : uint8_t { Push, DropDebt, KeepDebt };

// The comparison is local against server. A server copy that is ahead of another machine's reading needs no
// push (the wake pull brings it over); no copy at all is uploaded; an error keeps the debt for the next sleep.
// `serverIsOurs`: the server still holds the record this machine put there or took from there last, so no
// other machine wrote since. `placeChanged`: this machine's place moved since that record. When both hold the
// machine's own reading is the newest, even when it went back, so it is pushed.
constexpr PushAction pushAction(const Fetch fetch, const ProgressComparison comparison, const bool serverIsOurs,
                                const bool placeChanged) {
  if (fetch == Fetch::Failed) return PushAction::KeepDebt;
  if (fetch == Fetch::NotFound) return PushAction::Push;
  if (comparison == ProgressComparison::LocalAhead) return PushAction::Push;
  if (serverIsOurs && placeChanged && comparison != ProgressComparison::Synchronized) return PushAction::Push;
  return PushAction::DropDebt;
}

enum class PullAction : uint8_t { Apply, Keep };

// Only a server position that is ahead moves the book, and only when another machine wrote it: a record
// this machine itself left there is older than its own reading, however far ahead it looks. Equal, behind,
// unknown or failed: keep the page.
constexpr PullAction pullAction(const Fetch fetch, const ProgressComparison comparison, const bool serverIsOurs) {
  return fetch == Fetch::Found && comparison == ProgressComparison::RemoteAhead && !serverIsOurs ? PullAction::Apply
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
