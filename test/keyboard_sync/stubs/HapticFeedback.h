#pragma once
// The haptic motor is not part of the keyboard boundary: a touch action does nothing here.
namespace haptic_feedback {
inline void touchAction(bool = false) {}
}  // namespace haptic_feedback
