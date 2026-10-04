#pragma once

#include <Logging.h>

#if TENOR_COLD_LOG
#include <Arduino.h>

// X4 Pro probe build: probing with no working serial link. Commands come from a script on the
// card, /x4pro/lenh.txt, one line at a time through the same dispatcher as the cable's CMD: lines,
// and everything the probe prints is appended to /x4pro/log/nhat-ky.txt, read back over USB Drive.
// Script lines: "CMD:<command>", "WAIT <ms>" (the loop keeps running meanwhile), "# comment".
// Each line leaves the script before it runs (ColdScript.h), so a command that restarts or crashes
// the unit is not run again and the next boot goes on with the line after it.
namespace coldlog {

// Once per loop pass with no cable command waiting: the next script line to dispatch, or empty.
// Nothing happens before the first frame is up; then this boot's log goes to the card once.
String nextLine(bool firstFrameUp);
// Appends what the probe printed since the last flush, under a header line (boot, reset reason,
// millis, why). No-op while the card is not mounted (USB Drive owns it).
void flush(const char* why);

}  // namespace coldlog

#endif
