#pragma once
struct HalGPIO { bool x3 = false; bool deviceIsX3() const { return x3; } };
extern HalGPIO gpio;
