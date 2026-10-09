#pragma once

// X4 Pro: a newly installed image boots on trial when the bootloader supports rollback (otadata
// PENDING_VERIFY). It passes once its first frame is on the glass and touch is ready; a trial boot
// that fails its self-check is reset by a watchdog, and the bootloader then boots the previous image.
// A boot that is not on trial arms nothing. Other boards keep the Arduino default, which accepts any trial image
// before setup() runs.
namespace boot_trial {

// Start of setup(): arms the watchdog and returns true when this boot is on trial.
bool begin();
// After the first paint: accepts a touch-ready image, then stops the watchdog on success.
void passed(bool touchReady);

}  // namespace boot_trial
