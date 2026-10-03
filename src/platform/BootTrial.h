#pragma once

// X4 Pro: a newly installed image boots on trial when the bootloader supports rollback (otadata
// PENDING_VERIFY). It passes once its first frame is on the glass; a trial boot that hangs before
// that is reset by a watchdog, and the bootloader then boots the previous image. A boot that is
// not on trial arms nothing. Other boards keep the Arduino default, which accepts any trial image
// before setup() runs.
namespace boot_trial {

// Start of setup(): arms the watchdog when this boot is on trial.
void begin();
// The image was accepted after its first frame: stops the watchdog.
void passed();

}  // namespace boot_trial
