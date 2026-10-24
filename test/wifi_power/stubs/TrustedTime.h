#pragma once
// The loan-clock floor as the Wi-Fi chooser calls it. HalClock stays the only SNTP owner.
namespace trustedtime {
inline bool floorOnly = false;
inline int notes = 0;
inline int networkSyncs = 0;
inline bool clockNeedsSync() { return floorOnly; }
inline void noteNetworkSync() { ++networkSyncs; }
inline void note() { ++notes; }
}  // namespace trustedtime
