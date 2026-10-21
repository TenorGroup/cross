#pragma once

#include <cstdint>

// Only the settings the tilt route and the menu poll read.
struct CrossPointSettings {
  uint8_t tiltPageTurn = 0;
  uint8_t tiltTabNavigation = 0;
  uint8_t tiltMenuNavigation = 0;
  uint8_t orientation = 0;
  uint8_t tiltStrengthH = 1;
  uint8_t tiltStrengthV = 1;
  uint8_t shakeAction = 0;
  uint8_t shakeStrength = 1;
  uint8_t faceDownAction = 0;
  uint8_t faceUpAction = 0;
  uint8_t doubleTapAction = 0;
  uint8_t screenTapAction = 0;
  uint8_t edgeTapAction = 0;
};
extern CrossPointSettings SETTINGS;
