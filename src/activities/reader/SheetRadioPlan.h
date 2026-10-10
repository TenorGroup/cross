#pragma once

// What opening the reader sheet does with the radio and the page snapshot. Pure, so the host
// test can run every case.
//
// Today the sheet stops the radio and stores the 52 KB page snapshot: both need the heap the
// other holds. The P5b measurement build (TENOR_P5B) keeps a running radio under the sheet and
// skips the snapshot instead; every redraw of the sheet then draws the page again under it.
namespace sheetradio {

struct Plan {
  bool stopRadio;      // stop the radio before the sheet is drawn
  bool storeSnapshot;  // keep the clean page in RAM to wipe the sheet's chrome
};

// The ONE answer. keepRadio: the build keeps a running radio under the sheet.
constexpr Plan planFor(const bool keepRadio, const bool radioRunning) {
  if (keepRadio && radioRunning) return Plan{false, false};
  return Plan{true, true};
}

// A sheet drawn without a snapshot cannot wipe its old chrome: the page is drawn again instead.
constexpr bool redrawFromPage(const bool sheetKeptRadio, const bool snapshotStored) {
  return sheetKeptRadio && !snapshotStored;
}

}  // namespace sheetradio
