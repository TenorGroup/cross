#pragma once

// What opening the reader sheet does with the radio and the page snapshot. Pure, so the host
// test can run every case.
//
// The radio and the 52 KB page snapshot do not fit the X3 heap together. A running radio stays up
// under the sheet (the remote keeps its link) and the snapshot is not taken: every redraw of the
// sheet draws the page again under it. With the radio off the sheet stores its snapshot as before.
namespace sheetradio {

struct Plan {
  bool stopRadio;      // stop the radio before the sheet is drawn
  bool storeSnapshot;  // keep the clean page in RAM to wipe the sheet's chrome
};

// The ONE answer.
constexpr Plan planFor(const bool radioRunning) {
  if (radioRunning) return Plan{false, false};
  return Plan{true, true};
}

// A sheet drawn without a snapshot cannot wipe its old chrome: the page is drawn again instead.
constexpr bool redrawFromPage(const bool sheetKeptRadio, const bool snapshotStored) {
  return sheetKeptRadio && !snapshotStored;
}

}  // namespace sheetradio
