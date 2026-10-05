#pragma once

#include "activities/reader/ReaderTapZones.h"

class GfxRenderer;

// X4 Pro: the first time a book opens, a map of the page's tap zones drawn over it (the boxes
// readertap::zoneBox gives, for the back column in use and the orientation on screen). The reader opens
// and closes it; the common frame hook draws it while it is open.
namespace readertip {
void open(const readertap::Rules& rules);
void close();
bool isOpen();
const readertap::Rules& rules();
void draw(const GfxRenderer& renderer);
}  // namespace readertip
