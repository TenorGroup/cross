// Ban gia lap phan cung cho bo kiem chay tren may ban.
// Chi dinh nghia dung nhung ky hieu ma trinh lien ket doi, do bang phep thu lien ket
// ngay 13/09/2026: 20 ham cua HalGPIO, mot the hien den nen, va millis().
//
// KHONG dinh nghia lai bat ky hanh vi nao cua MappedInputManager. File dang kiem
// duoc bien dich NGUYEN VAN tu src/MappedInputManager.cpp.
#include <HalFrontlight.h>
#include <HalGPIO.h>

#include <cstdint>

namespace faketest {
// Trang thai nut do bai kiem dat. Chi so theo HalGPIO::BTN_*.
bool pressed[8] = {false, false, false, false, false, false, false, false};
bool released[8] = {false, false, false, false, false, false, false, false};
bool held[8] = {false, false, false, false, false, false, false, false};
unsigned long heldMs = 0;
bool touch = false;
bool homeKey = false;

void reset() {
  for (int i = 0; i < 8; ++i) {
    pressed[i] = released[i] = held[i] = false;
  }
  heldMs = 0;
}
}  // namespace faketest

unsigned long millis() { return 1000; }

InputManager::InputManager() {}

bool HalGPIO::wasPressed(uint8_t i) const { return i < 8 && faketest::pressed[i]; }
bool HalGPIO::wasReleased(uint8_t i) const { return i < 8 && faketest::released[i]; }
bool HalGPIO::isPressed(uint8_t i) const { return i < 8 && faketest::held[i]; }
bool HalGPIO::wasAnyPressed() const {
  for (int i = 0; i < 8; ++i) {
    if (faketest::pressed[i]) return true;
  }
  return false;
}
bool HalGPIO::wasAnyReleased() const {
  for (int i = 0; i < 8; ++i) {
    if (faketest::released[i]) return true;
  }
  return false;
}
unsigned long HalGPIO::getHeldTime() const { return faketest::heldMs; }
bool HalGPIO::hasTouch() const { return faketest::touch; }
bool HalGPIO::hasHomeKey() const { return faketest::homeKey; }
bool HalGPIO::wasHomeKeyTapped() const { return false; }
bool HalGPIO::wasHomeKeyLongPressed() const { return false; }
void HalGPIO::update() {}
bool HalGPIO::wasTouchTap(float&, float&) const { return false; }
bool HalGPIO::wasTouchReleased() const { return false; }
bool HalGPIO::wasTouchLongPress(float&, float&) const { return false; }
bool HalGPIO::isTouchTapCandidate(float&, float&, unsigned long&) const { return false; }
bool HalGPIO::isTouchHeldAt(float&, float&) const { return false; }
unsigned long HalGPIO::lastTouchHeldMs() const { return 0; }
void HalGPIO::suppressTouchContact() {}
bool HalGPIO::wasSwipe(float&, float&, float&, float&) const { return false; }
