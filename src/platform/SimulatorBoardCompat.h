#pragma once

// The simulator ships its own trimmed BoardConfig that shadows the SDK one, and
// that copy predates a few of the SDK's queries. Fill the gaps here rather than
// scattering #ifdef SIMULATOR through the firmware. Device builds get the real
// SDK header and never see this file's contents.
#ifdef SIMULATOR
#include <BoardConfig.h>

namespace BoardConfig {
// No simulated board wires its light over I2C; only the X4 Pro does, and the
// simulator drives its panel through SDL rather than a light controller.
inline bool hasI2cFrontlight() { return false; }
}  // namespace BoardConfig
#endif
