#pragma once

class GfxRenderer;

namespace freeink::ble {

// Attempts one bounded BLE HID host start for every app entry point.
// The renderer is used only to release rebuildable SD-font caches while the
// render lock is held before measuring the internal heap.
// A successful stack start is rolled back if it leaves less than one 32 KiB
// contiguous block for the reader's inflate window.
bool begin(GfxRenderer& renderer);

// Kick the same one bounded attempt on a short-lived worker task so the main
// loop never stalls on NimBLE/controller init. Returns false immediately when
// an attempt is already running. The result is observable through the host's
// own state (isRunning) and the diagnostics status command.
bool beginAsync(GfxRenderer& renderer);

// Poll teardown without blocking input. A false result defers the activity
// transition until the worker has released its memory and callbacks.
bool suspendForTransition();

// True when the reader's own attempt to start the radio was turned down for
// memory (CROSSPOINT_BLE_HID_HOST builds call setReaderStartDeferred when the
// async start reports a failure). The settings screen reads it to say "not
// running in books: low memory" instead of claiming the page turner is on.
// Lives outside the capability guard: a build without BLE still answers false.
bool readerStartDeferred();
void setReaderStartDeferred(bool deferred);

}  // namespace freeink::ble
