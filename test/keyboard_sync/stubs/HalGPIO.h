#pragma once
namespace keyboard_test {
extern unsigned long nowMs;
}
struct FakeGPIO { bool deviceIsX3() const { return true; } };
inline FakeGPIO gpio;
inline unsigned long millis() { return keyboard_test::nowMs; }
