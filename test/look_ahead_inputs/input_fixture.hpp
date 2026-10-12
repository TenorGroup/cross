#include "FakeBle.h"
#include "HidDescriptors.h"
#include "BlePageTurner.h"
#include "RadioPort.h"
#include "Runtime.h"
#include <atomic>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <climits>

constexpr int LOW = 0, HIGH = 1;
std::array<int, 64> pins;
std::array<int, 3> adc{4095, 4095, 4095};
int digitalRead(int pin) { return pin < 0 ? HIGH : pins.at(pin); }
int analogRead(int pin) { return adc.at(pin); }
int analogReadMilliVolts(int pin) { return analogRead(pin); }
namespace BoardConfig {
enum class InputStyle { XteinkAdcLadder, OnePageAdcLadder, DigitalButtons,
                       DigitalConfirmBackHold, DigitalConfirmPowerHold, DigitalTwoButton };
struct Pins { int back=-1, confirm=-1, left=-1, right=-1, up=0, down=7, power=3; bool powerActiveHigh=false; int adcLadderPin=-1; };
struct Profile { InputStyle inputStyle=InputStyle::DigitalButtons; Pins input; } ACTIVE;
}
struct InputManager {
  static constexpr int BTN_BACK=0, BTN_CONFIRM=1, BTN_LEFT=2, BTN_RIGHT=3, BTN_UP=4, BTN_DOWN=5, BTN_POWER=6;
  static constexpr int BUTTON_ADC_PIN_1=1, BUTTON_ADC_PIN_2=2, ADC_NO_BUTTON=3900, NUM_BUTTONS_1=4, NUM_BUTTONS_2=2;
  static constexpr unsigned long DEBOUNCE_DELAY=5;
  static const int ADC_RANGES_1[], ADC_RANGES_2[];
  using ButtonHook = uint8_t (*)();
  static inline ButtonHook s_buttonHook=nullptr;
  static void setButtonHook(ButtonHook hook) { s_buttonHook=hook; }
  struct ButtonAdcSample { int pin, raw, button; };
  uint8_t currentState=0, lastState=0, pressedEvents=0, releasedEvents=0;
  unsigned long lastDebounceTime=0, buttonPressStart=0, buttonPressFinish=0, powerButtonPressStart=0, powerButtonPressFinish=0;
  bool touchPressedEvent=false, touchReleasedEvent=false, touchLongPressEvent=false,
       multiTouchSwipeEvent=false, multiTouchRotationEvent=false, multiTouchPinchEvent=false,
       touchHomeKeyEvent=false, touchHomeKeyTapEvent=false, touchHomeKeyLongEvent=false;
  int getButtonFromADC(int, const int[], int);
  void readButtonAdc(ButtonAdcSample&, ButtonAdcSample&);
  uint8_t getState(); uint8_t getDigitalState() const;
  bool isDigitalPressed(int8_t) const;
  bool isPowerButtonPhysicallyPressed() const;
  void applyStateChange(uint8_t, unsigned long); void update();
  bool wasAnyPressed() const; bool wasAnyReleased() const; bool isPressed(uint8_t) const;
  uint8_t serviceTouch() { return 0; }
  void updateConfirmBackHold(unsigned long) { throw std::runtime_error("unexpected board"); }
  void updateConfirmPowerHold(unsigned long) { throw std::runtime_error("unexpected board"); }
  void updateDigitalTwoButton(unsigned long) { throw std::runtime_error("unexpected board"); }
};
struct HalGPIO {
  InputManager inputMgr;
  bool sampleTimer=false;
  bool lastUsbConnected=false, usbStateChanged=false;
  void injectPresses(uint8_t, uint16_t, uint16_t, uint16_t);
  void update(); bool rawInputActive();
  bool isUsbConnected() const { return false; }
  bool wasUsbStateChanged() const { return usbStateChanged; }
  bool wasTouchActivity() const { return false; }
  bool wasAnyPressed() const { return inputMgr.wasAnyPressed(); }
  bool wasAnyReleased() const { return inputMgr.wasAnyReleased(); }
} gpio;
struct Frames {
  void beginFrame() { throw std::runtime_error("timer path outside this projection"); }
  bool active() const { throw std::runtime_error("timer path outside this projection"); }
} frames;
portMUX_TYPE edgeLock;
std::function<void()> quantumHook;
int mainPolls=0, lastBuildPoll=-1;
void beforeQuantum() {
  if (lastBuildPoll == mainPolls) throw std::runtime_error("second build quantum before main input poll");
  lastBuildPoll=mainPolls;
}
void afterQuantum() { if (quantumHook) quantumHook(); }
