#pragma once

// Boards with the cable flash channel (TENOR_CABLE_FLASH: the X4 Pro and, from v1.1.0, the X4 Classic): a newly
// installed image boots on trial when the bootloader supports rollback (otadata PENDING_VERIFY). It passes once
// its first frame is on the glass, its cable port is up (the only way to flash a unit whose USB download mode
// is fused off) and, on the X4 Pro, touch is ready; a trial boot that fails its self-check is reset by a
// watchdog, and the bootloader then boots the previous image. A boot that is not on trial arms nothing. Other
// boards keep the Arduino default, which accepts any trial image before setup() runs.
namespace boot_trial {

// Start of setup(): arms the watchdog and returns true when this boot is on trial.
bool begin();
// After the first paint: accepts an image whose cable port is up and whose touch is ready (true on a board
// without a touch panel), then stops the watchdog on success.
void passed(bool touchReady, bool cablePortUp);

}  // namespace boot_trial
