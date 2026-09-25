#pragma once
struct InputManager {
  struct ButtonAdcSample { int pin, raw, button; };
};
class HalGPIO {
 public:
  static constexpr uint8_t BTN_DOWN = 5;
  void sampleButtonAdc(InputManager::ButtonAdcSample&, InputManager::ButtonAdcSample&);
};
