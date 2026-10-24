#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session. The live frontlight
// state rides along in the same flag so the reboot is invisible: the light
// comes back exactly as it was, regardless of the Restore Light on Wake
// preference.

void silentRestart();            // home screen
void silentRestartToReader();    // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToSettings();  // settings screen
// Wi-Fi tasks that end in a restart anyway run on a fresh boot's heap: the clock sync with its time zone lookup and
// the KOReader login (TLS wants ~35-40 KB with Wi-Fi up; under Settings the X3 had 30-32 KB, refused).
enum class FreshBootTask : uint8_t { ClockSync, KOReaderLogin, KOReaderSignUp };
// The one rule: an opening not made by that boot, with Wi-Fi not joined yet, restarts into the task's boot and
// returns true (the caller finishes). False: run here (the task's own boot, Wi-Fi up, the simulator, or sleep won).
bool startsOnFreshBoot(bool freshBoot, FreshBootTask task);
void silentRestartToFontStore();     // Tải font, inside Connections (out of a download or an update)
void silentRestartToFileTransfer();  // Connections, the File Transfer hub (out of the font store)
// The open book's card stopped answering (CardRescue.h): restart into the book with nothing
// written first, since every write would wait out the dead card. Returns only if sleep won.
void silentRestartForCard();
// The update boot (network/UpdateBoot.h): the confirmed install when dryRunsLeft is 0, else the
// probe's next dry run. Callers: the update screen after the user chose Update, and CMD:OTA_DRYRUN.
void silentRestartToOta(uint8_t dryRunsLeft = 0, uint8_t dryRunsTotal = 0);
// Reboot into the File Transfer > Join Network flow on a pristine heap, so the
// WiFi + TLS working set has the contiguous RAM it needs on tight boards. A
// no-op on touch boards (a soft reset would cycle their externally-powered
// rails); the caller then proceeds without a reboot.
void silentRestartToJoinNetwork();

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
