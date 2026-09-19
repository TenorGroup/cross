#pragma once
struct InputManager {
  struct ButtonAdcSample { int pin, raw, button; };
};
class HalGPIO {
 public:
  void sampleButtonAdc(InputManager::ButtonAdcSample&, InputManager::ButtonAdcSample&);
};
