#pragma once
struct FakeGPIO { bool deviceIsX3() const { return true; } };
inline FakeGPIO gpio;
inline unsigned long millis() { return 10000; }
