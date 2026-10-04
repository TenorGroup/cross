#pragma once
struct HalGPIO { bool hasTouch() const { return false; } };
inline HalGPIO gpio;
