#pragma once
// The words of tenor/ugly that screens outside the shell show. The voice of the shell stays in the shell: no file
// outside src/shells/ugly names one of its strings (test/ugly_shell/check_assets.py).

namespace ugly::words {

// The screen after a crash: its title, what happened, the line over the reason.
const char* crashTitle();
const char* crashBody();
const char* crashReason();
// Clear cache, done or failed.
const char* cacheDone();
const char* cacheFail();
// While the update from the card writes the firmware.
const char* flashing();

}  // namespace ugly::words
