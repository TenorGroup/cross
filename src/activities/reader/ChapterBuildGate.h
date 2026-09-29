#pragma once

#include <BlePageTurner.h>

// Whether a chapter build may run next to the page turner's radio. A radio that did not stop in
// time, is still stopping, or is still starting holds or is taking the heap the build needs, and
// a build that runs out of memory aborts the firmware (no exceptions). A radio refused for memory
// holds nothing.
inline bool chapterBuildMayRun(const bleturner::Status& radio) {
  return !radio.running && !radio.stopping && !radio.starting;
}
