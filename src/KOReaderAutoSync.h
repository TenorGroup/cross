#pragma once

#include <cstdint>
#include <string>

#include "CrossPointPosition.h"

class GfxRenderer;

// Automatic KOReader sync. Off until the user turns "Automatic sync" on in the KOReader screen, and
// then only with an account and a saved Wi-Fi network. It never opens a Wi-Fi list, a picker or a
// question: when anything is missing or goes wrong the round is skipped and the book stays where it is.
namespace koautosync {

// Is a left book worth noting at all (switch on and an account)? Asked before any work is done for it.
bool wantsNote();

// The reader is closing and still holds the Epub: remember that this book is owed a push. Reads
// nothing from the card; the debt file is written after the next screen's first frame (during
// sleep, at once).
void noteLeftBook(const std::string& path, const CrossPointPosition& position, float percentage, bool sleeping);

// Writes the debt noted by noteLeftBook if it is still in memory. Deferred-write entry point.
void flushNote();

// Deep sleep, after the sleep screen and the retained frame are done and before the panel and the
// card power down: pushes the books owed to the server, within kosyncauto::ROUND_BUDGET_MS.
void pushBeforeSleep(GfxRenderer& renderer);

// The switch was turned off: what was owed is forgotten, in memory and on the card, so turning it on again
// later pushes nothing from before.
void switchedOff();

// What to store as APP_STATE.sleepStartedAt when going to sleep: now, or 0 when automatic sync is off
// or the clock cannot be trusted (then the next wake counts as a short sleep and pulls nothing).
uint32_t sleepStamp();

enum class PullResult : uint8_t { Skipped, Kept, Applied, Failed };

// Wake into a book, before its page is drawn: only after a sleep of at least
// kosyncauto::PULL_MIN_SLEEP_SECONDS. Asks the server where the book is and, when that is ahead of the
// saved place, writes the server's place as the saved place. `showNotice`: the kept sleep frame is on
// the glass, so the "Syncing" popup goes over it for the wait. Joins the saved network and leaves the
// radio off when it returns. The caller opens the book afterwards as usual, whatever the result.
PullResult pullOnWake(GfxRenderer& renderer, const std::string& path, bool showNotice);

// Set when pullBeforeOpen moved the saved place: where the book was and where it is now. For the
// back-stack entry that returns the reader to the page it was on.
struct PullChange {
  std::string path;
  int fromSpineIndex = 0;
  int fromPage = 0;
  int toSpineIndex = 0;
  int toPage = 0;
  uint32_t fromOffset = 0;  // the text offset the reader was at, when the saved place had one
  bool hasFromOffset = false;
};
// The pull for the first epub opened after a long sleep that ended on Home (a book opened from Home, Recent or
// the file browser). Called once per start in setup(): `quietRestart` is a silent reboot, `wokeOnHome` a wake
// that lands on Home. A long sleep with the switch on leaves the pull owed in RTC memory; any other start clears it.
void noteStart(bool quietRestart, bool wokeOnHome);

// Asked by every open of a book, before the reader is made. true: the open was handed to a quiet restart into
// this book (the owed pull runs after it, on a fresh heap) and the caller must not open it. false: open it; when
// the restart has just happened the pull has already run.
bool beforeOpen(GfxRenderer& renderer, const std::string& path);

// Roll back an armed open when its restart activity could not be allocated. The next epub open may retry
// the quiet restart with the same one-shot debt, and no stale path or spent radio memo remains.
void cancelPullRestart();

// The restart that beforeOpen chose is on its way: its notice reads "Syncing" rather than "Loading".
bool pullIsArmed();
// The sync notice is already on the glass; keep it through the heap restart.
bool pullRestartReady();

#ifdef TENOR_PRESS_PROBE
// Measuring build only. Lets a timed wake count as a long sleep: the 30 minute rule is read as `seconds`
// from now on (0 puts the rule back), kept through deep sleep. Asked by the cable command KAS_MIN_SLEEP.
void probeSetMinSleep(uint32_t seconds);
// Once per pull that ran: prints the heap after the book's first frame (the line after the pull is
// printed by pullOnWake itself, before the book opens).
void probeAfterFirstFrame();
#endif

const PullChange* progressJustPulled();
void acknowledgeProgressPulled();

}  // namespace koautosync
