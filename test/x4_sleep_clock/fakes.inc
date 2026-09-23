// GPIO, clock and ESP-IDF sleep boundaries shared by behavior.cpp and journey.cpp.
// Every call the production sleep path makes is recorded in fake::trace.
namespace fake {
std::vector<std::string> trace;
std::array<int, 64> levels;
std::array<bool, 64> held;
unsigned long ms = 0;
int pressedReads = 0, lightSleeps = 0;
struct Sleep {};
void event(const std::string& value) { trace.push_back(value); }
void pin(const char* action, int pin, int value = -1) {
  event(std::string(action) + ":" + std::to_string(pin) + ":" + std::to_string(value));
}
void reset(BoardConfig::Board board) {
  trace.clear(); levels.fill(-1); held.fill(false); ms = 0; pressedReads = lightSleeps = 0;
  BoardConfig::ACTIVE = {};
  BoardConfig::ACTIVE.board = board;
  if (board == BoardConfig::Board::X3) {
    BoardConfig::ACTIVE.power.latch0 = -1;
    BoardConfig::ACTIVE.sd.powerEnable = 13;
  } else if (board == BoardConfig::Board::Other) {
    BoardConfig::ACTIVE.power.latch0 = 1;
    BoardConfig::ACTIVE.display.rst = 9;
    BoardConfig::ACTIVE.sd.powerEnable = 5;
    BoardConfig::ACTIVE.sd.powerActiveHigh = false;
  }
  // Retained holds must be released before a new desired level can take effect.
  held[13] = true; levels[13] = 0;
}
}
SerialStub logSerial;
void SerialStub::end() { fake::event("serial-end"); }
void pinMode(int pin, int mode) { fake::pin("mode", pin, mode); }
void digitalWrite(int pin, int value) {
  fake::pin("write", pin, value);
  if (!fake::held[pin]) fake::levels[pin] = value;
}
int digitalRead(int pin) {
  fake::pin("read", pin);
  return fake::pressedReads-- > 0 ? LOW : HIGH;
}
unsigned long millis() { return fake::ms; }
void delay(unsigned long ms) { fake::ms += ms; fake::event("delay:" + std::to_string(ms)); }
int getCpuFrequencyMhz() { return 160; }
bool setCpuFrequencyMhz(int) { return true; }
void gpio_hold_dis(int pin) { fake::pin("release", pin); fake::held[pin] = false; }
void gpio_hold_en(int pin) { fake::pin("hold", pin); fake::held[pin] = true; }
void gpio_set_direction(int pin, int mode) { fake::pin("direction", pin, mode); }
void gpio_set_level(int pin, int level) { digitalWrite(pin, level); }
void gpio_deep_sleep_hold_en() { fake::event("global-hold"); }
int esp_sleep_disable_wakeup_source(int) { fake::event("disable-wake"); return ESP_OK; }
int esp_sleep_enable_timer_wakeup(uint64_t us) { fake::event("timer:" + std::to_string(us)); return ESP_OK; }
int esp_light_sleep_start() { ++fake::lightSleeps; return 1; }
int64_t esp_timer_get_time() { return static_cast<int64_t>(fake::ms) * 1000; }
int esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int level) {
  fake::event("gpio-wake:" + std::to_string(mask) + ":" + std::to_string(level)); return ESP_OK;
}
int esp_sleep_enable_ext1_wakeup(uint64_t mask, int level) {
  fake::event("ext1-wake:" + std::to_string(mask) + ":" + std::to_string(level)); return ESP_OK;
}
void esp_sleep_config_gpio_isolate() { fake::event("isolate"); }
[[noreturn]] void esp_deep_sleep_start() { fake::event("sleep"); throw fake::Sleep{}; }
