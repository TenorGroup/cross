#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

inline unsigned long testMillis = 0;
inline unsigned long millis() { return testMillis; }
inline void delay(const unsigned long ms) { testMillis += ms; }

enum StrId {
  STR_ADD_HIDDEN_NETWORK,
  STR_BACK,
  STR_CANCEL,
  STR_CONNECT,
  STR_CONNECTED,
  STR_CONNECTING,
  STR_CONNECTING_SAVED_WIFI,
  STR_CONNECTION_FAILED,
  STR_DIR_LEFT,
  STR_DIR_RIGHT,
  STR_DONE,
  STR_ENTER_WIFI_PASSWORD,
  STR_ENTER_WIFI_SSID,
  STR_ERROR_CONNECTION_TIMEOUT,
  STR_ERROR_GENERAL_FAILURE,
  STR_ERROR_NETWORK_NOT_FOUND,
  STR_FINDING_SAVED_WIFI,
  STR_FORGET_AND_REMOVE,
  STR_FORGET_BUTTON,
  STR_FORGET_NETWORK,
  STR_IP_ADDRESS_PREFIX,
  STR_MAC_ADDRESS,
  STR_NETWORKS_FOUND,
  STR_NETWORK_LEGEND,
  STR_NETWORK_PREFIX,
  STR_NO,
  STR_NO_NETWORKS,
  STR_PRESS_OK_SCAN,
  STR_RETRY,
  STR_SAVE_PASSWORD,
  STR_SCANNING,
  STR_SELECT,
  STR_SHOW_NETWORKS,
  STR_TO_PREFIX,
  STR_UGLY_WIFI_CONNECTED,
  STR_UGLY_WIFI_CONNECTING,
  STR_UGLY_WIFI_FAILED,
  STR_UGLY_WIFI_SCANNING,
  STR_UGLY_WIFI_TIP,
  STR_UGLY_WIFI_NONE,
  STR_UGLY_WIFI_SAVE_ASK,
  STR_UGLY_WIFI_SAVE_YES,
  STR_UGLY_WIFI_SAVE_NO,
  STR_UGLY_WIFI_FORGET_ASK,
  STR_UGLY_WIFI_FORGET_YES,
  STR_UGLY_WIFI_FORGET_NO,
  STR_WIFI_NETWORKS,
  STR_WIFI_NOT_IN_RANGE,
  STR_YES,
};

inline const char* tr(const StrId id) {
  if (id == STR_NETWORKS_FOUND) return "%zu networks";
  if (id == STR_SHOW_NETWORKS) return "Mang";
  if (id == STR_MAC_ADDRESS) return "MAC";
  if (id == STR_NETWORK_PREFIX) return "Network: ";
  if (id == STR_IP_ADDRESS_PREFIX) return "IP: ";
  if (id == STR_TO_PREFIX) return "To: ";
  return "text";
}

#define LOG_ERR(...)
#define LOG_INF(...)
#define LOG_DBG(...)

struct Rect {
  int x = 0;
  int y = 0;
  int width = 480;
  int height = 800;
};

struct ThemeMetrics {
  int topPadding = 0;
  int headerHeight = 40;
  int tabBarHeight = 20;
  int verticalSpacing = 4;
  int listRowHeight = 40;
  int contentSidePadding = 8;
  int popupFrameThickness = 1;
  int popupCornerRadius = 1;
};

enum class EpdFontFamily { BOLD };
constexpr int UI_10_FONT_ID = 10;
constexpr int UI_12_FONT_ID = 12;
constexpr int SMALL_FONT_ID = 8;

class FontCacheManager {
 public:
  void releaseSdFontCaches() {}
};

class GfxRenderer {
 public:
  FontCacheManager cache;
  void clearScreen() {}
  void displayBuffer() {}
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int) const { return 20; }
  int getTextWidth(int, const char*) const { return 20; }
  void drawText(int, int, int, const char*) const {}
  FontCacheManager* getFontCacheManager() { return &cache; }
};

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Up, Down, Left, Right };
  enum class SwipeDir { None, Up, Down };
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  std::optional<Button> pressed;
  std::optional<Button> released;
  bool confirmDown = false;
  bool backDown = false;
  SwipeDir swipe = SwipeDir::None;
  bool touch = false;

  bool wasPressed(const Button button) {
    return pressed == button;
  }
  bool wasReleased(const Button button) {
    return released == button;
  }
  bool isPressed(const Button button) const {
    return (button == Button::Confirm && confirmDown) || (button == Button::Back && backDown);
  }
  SwipeDir wasSwipe() {
    const auto value = swipe;
    swipe = SwipeDir::None;
    return value;
  }
  bool hasTouch() const { return touch; }
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
};

struct WifiResult {
  bool connected;
  std::string ssid;
  std::string ip;
};
struct KeyboardResult {
  std::string text;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, WifiResult, KeyboardResult> data;
};
using ActivityResultHandler = std::function<void(const ActivityResult&)>;

class RenderLock {
 public:
  template <typename T>
  explicit RenderLock(T&) {}
};

class Activity {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  inline static bool didFinish = false;
  inline static bool didRequestUpdate = false;
  std::unique_ptr<Activity> child;
  ActivityResultHandler childHandler;
  ActivityResult finalResult;

  Activity(const char*, GfxRenderer& renderer, MappedInputManager& input) : renderer(renderer), mappedInput(input) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  virtual bool preventAutoSleep() { return false; }
  void requestUpdate(bool = false) { didRequestUpdate = true; }
  void startActivityForResult(std::unique_ptr<Activity>&& next, ActivityResultHandler handler) {
    child = std::move(next);
    childHandler = std::move(handler);
  }
  void setResult(ActivityResult&& value) { finalResult = std::move(value); }
  static void finish() { didFinish = true; }
};

namespace freeink::ui {
using ActionId = int16_t;
struct ActionEvent {
  int16_t value = 0;
  bool longPress = false;
};
struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 480;
  int16_t height = 800;
  bool empty() const { return width <= 0 || height <= 0; }
};
struct Insets {
  int16_t top, right, bottom, left;
};
struct Size {
  int16_t width, height;
};
enum State { StateNormal, StateFocused };
enum Color { Black };
struct Paint {
  static Paint solid(Color) { return {}; }
};
struct TextStyle {
  int maxLines = 1;
  bool bold = false;
};
struct Style {
  Paint border;
  uint8_t borderWidth = 0;
  uint8_t radius = 0;
};
struct Styles {
  Style normal, selected, focused, active, disabled;
};
inline Styles defaultPopupStyles() { return {}; }
constexpr uint8_t InputTouch = 1;
constexpr uint8_t InputLongPress = 2;
struct ListItem {
  const char* label = nullptr;
  const char* value = nullptr;
  int16_t actionValue = 0;
};
struct ListProps {
  ListItem* items = nullptr;
  uint16_t count = 0;
  ActionId action = 0;
  uint8_t inputMask = 0;
  int valueInset = 0;
  TextStyle labelText;
  bool balanceWrappedLabelWithValue = true;
  bool partialTrailingRow = false;
  int selected = 0;
  int top = 0;
  int16_t rowHeight = 0;
};
struct ButtonProps {
  const char* label = nullptr;
  ActionId action = 0;
  uint8_t inputMask = 0;
  TextStyle text;
};
struct DialogOption {
  const char* label = nullptr;
  ActionId action = 0;
  int16_t value = 0;
  State state = StateNormal;
};
struct OptionDialogProps {
  const char* title = nullptr;
  const char* headline = nullptr;
  const char* message = nullptr;
  DialogOption* options = nullptr;
  int optionCount = 0;
  bool verticalOptions = false;
  TextStyle titleText, headlineText, messageText, buttonText;
  uint8_t inputMask = 0;
  Styles styles;
};
struct Theme {
  TextStyle bodyText, smallText;
  int16_t rowHeight = 40;
  int16_t listRowGap = 0;
  int16_t spaceMd = 8;
};
struct Frame {};
struct Target {};
class UiScreen {
 public:
  Theme valueTheme;
  Rect content;
  Frame valueFrame;
  Target valueTarget;
  void setContentMarginFromScreen(Insets) {}
  void centeredText(const char*, TextStyle) {}
  Theme& theme() { return valueTheme; }
  Rect body() const { return content; }
  // Same bounded bottom reservation as the SDK Screen, with fake display I/O.
  Rect takeBottom(int16_t height) {
    height = std::clamp<int16_t>(height, 0, content.height);
    const Rect band{content.x, static_cast<int16_t>(content.y + content.height - height), content.width, height};
    content.height = static_cast<int16_t>(content.height - height);
    return band;
  }
  Frame& frame() { return valueFrame; }
  Target& target() { return valueTarget; }
  void list(const ListProps&) {}
  // The list viewport sync (#3668): hands the selection to the props, like ListNav::syncToProps.
  template <typename Nav>
  void syncListViewport(Nav& nav, ListProps& props, int) {
    props.selected = nav.selected;
  }
};
inline void button(Frame&, Rect, const ButtonProps&) {}
inline void optionDialog(Frame&, Rect, const OptionDialogProps&) {}
inline int16_t optionDialogHeight(Target&, const OptionDialogProps&, int16_t) { return 100; }
inline Rect centeredRect(Rect r, Size size) { return {r.x, r.y, size.width, size.height}; }

struct ListNav {
  int selected = 0;
  int visibleRows = 5;
  void reset() { selected = 0; }
  void follow(int) {}
  bool scrollBy(int, int) { return false; }
  void syncToProps(Rect, int16_t, int16_t, int, ListProps& props) { props.selected = selected; }
};
}  // namespace freeink::ui

using UiScreen = freeink::ui::UiScreen;

struct RouteResult {
  bool routed = false;
  explicit operator bool() const { return routed; }
};
class FakeUiApp {
 public:
  template <typename Callback>
  void on(int, Callback, void*) {}
  template <typename Callback>
  void setScreen(Callback, void*) {}
  void clearTapFlash() {}
  bool invalidated() const { return false; }
  freeink::ui::Rect publishedRect(int, int16_t) const { return {}; }
};
struct FakeUiTarget {
  void setPaintingEnabled(bool) {}
};
class UiAppHost {
 protected:
  FakeUiTarget uiTarget;
  FakeUiApp app;
  explicit UiAppHost(GfxRenderer&) {}
  // The production X3 chrome compiles out this touch-only branch.
  int swipeRows(MappedInputManager&, freeink::ui::ListNav&, int, int);
  static void followStep(freeink::ui::ListNav& nav, int count) { nav.follow(count); }
  void resetUi() {}
  void renderUi() {}
  RouteResult routeTouch(MappedInputManager&, bool = false) { return {}; }
};

class ButtonNavigator {
 public:
  template <typename F>
  void onNext(F&&) {}
  template <typename F>
  void onPrevious(F&&) {}
  static int nextIndex(size_t index, size_t count) { return count ? static_cast<int>((index + 1) % count) : 0; }
  static int previousIndex(size_t index, size_t count) { return count ? static_cast<int>((index + count - 1) % count) : 0; }
};

enum class InputType { Text, Password, Url };
class KeyboardEntryActivity : public Activity {
 public:
  InputType capturedType;
  bool capturedBackCancels;
  KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& input, std::string, std::string, size_t,
                        InputType type = InputType::Text, bool backCancels = false)
      : Activity("Keyboard", renderer, input), capturedType(type), capturedBackCancels(backCancels) {}
};

struct WifiCredential {
  std::string ssid;
  std::string password;
};
class WifiCredentialStore {
 public:
  std::vector<WifiCredential> credentials;
  std::string last;
  void loadFromFile() {}
  size_t getCredentialCount() const { return credentials.size(); }
  std::string getLastConnectedSsid() const { return last; }
  std::optional<WifiCredential> findCredential(const std::string& ssid) const {
    for (const auto& value : credentials)
      if (value.ssid == ssid) return value;
    return std::nullopt;
  }
  bool hasSavedCredential(const std::string& ssid) const { return findCredential(ssid).has_value(); }
  std::optional<std::string> getSsidAt(size_t index) const {
    if (index >= credentials.size()) return std::nullopt;
    return credentials[index].ssid;
  }
  void addCredential(const std::string& ssid, const std::string& password) { credentials.push_back({ssid, password}); }
  void removeCredential(const std::string& ssid) {
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(), [&](const auto& value) {
                        return value.ssid == ssid;
                      }),
                      credentials.end());
  }
  void setLastConnectedSsid(const std::string& ssid) { last = ssid; }
};
inline WifiCredentialStore WIFI_STORE;

enum wifi_mode_t { WIFI_MODE_NULL, WIFI_STA, WIFI_OFF = WIFI_MODE_NULL };
enum wl_status_t { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_CONNECTED, WL_CONNECT_FAILED };
constexpr int16_t WIFI_SCAN_RUNNING = -1;
constexpr int16_t WIFI_SCAN_FAILED = -2;
constexpr int WIFI_AUTH_OPEN = 0;
constexpr int WIFI_ALL_CHANNEL_SCAN = 0;
constexpr int WIFI_CONNECT_AP_BY_SIGNAL = 0;
class IPAddress {
 public:
  uint8_t bytes[4] = {192, 168, 1, 2};
  uint8_t operator[](size_t index) const { return bytes[index]; }
};
class WiFiClass {
 public:
  struct Network {
    std::string ssid;
    int rssi;
    int encryption;
  };
  wifi_mode_t currentMode = WIFI_MODE_NULL;
  int16_t scanState = WIFI_SCAN_RUNNING;
  wl_status_t connectionStatus = WL_IDLE_STATUS;
  std::vector<Network> found;
  int disconnectCalls = 0;
  int beginCalls = 0;
  bool lastErase = false;
  bool connectWhenStationReady = false;
  void reset() { *this = WiFiClass{}; }
  void persistent(bool) {}
  bool mode(wifi_mode_t mode) {
    currentMode = mode;
    return true;
  }
  wifi_mode_t getMode() const { return currentMode; }
  bool disconnect(bool off = false, bool erase = false, unsigned long = 100) {
    disconnectCalls++;
    lastErase = erase;
    connectionStatus = WL_IDLE_STATUS;
    if (off) currentMode = WIFI_MODE_NULL;
    return true;
  }
  int16_t scanNetworks(bool) {
    scanState = WIFI_SCAN_RUNNING;
    return scanState;
  }
  int16_t scanComplete() const { return scanState; }
  void scanDelete() {}
  std::string SSID(int index) const { return found.at(static_cast<size_t>(index)).ssid; }
  int32_t RSSI(int index) const { return found.at(static_cast<size_t>(index)).rssi; }
  int32_t RSSI() const { return -42; }
  int encryptionType(int index) const { return found.at(static_cast<size_t>(index)).encryption; }
  void begin(const char*, const char* = nullptr) {
    beginCalls++;
    connectionStatus = connectWhenStationReady && currentMode == WIFI_STA ? WL_CONNECTED : WL_IDLE_STATUS;
  }
  wl_status_t status() const { return connectionStatus; }
  IPAddress localIP() const { return {}; }
  void BSSID(uint8_t* out) const { std::fill(out, out + 6, 0); }
  int channel() const { return 1; }
  void setScanMethod(int) {}
  void setSortMethod(int) {}
  void setHostname(const char*) {}
};
inline WiFiClass WiFi;

class HalPowerManager {
 public:
  bool saving = false;
  int normalRequests = 0;
  void setPowerSaving(bool enabled) {
    saving = enabled;
    if (!enabled) normalRequests++;
  }
};
inline HalPowerManager powerManager;

class HalClock {
 public:
  bool available = false;
  bool valid = false;
  bool syncResult = true;
  int syncCalls = 0;
  bool isAvailable() const { return available; }
  bool hasValidTime() const { return valid; }
  bool syncFromNTP() {
    syncCalls++;
    valid = syncResult;
    return syncResult;
  }
};
inline HalClock halClock;

struct CrossPointSettings {
  uint8_t uiTextSize = 0;
  int clockHasBeenSynced = 0;
  int clockAutoTimezone = 0;
  uint8_t clockUtcOffsetQ = 48;
  int saves = 0;
  void saveToFile() { saves++; }
};
inline CrossPointSettings SETTINGS;

namespace timezone_lookup {
inline int calls = 0;
inline bool result = false;
inline bool updateOffset() {
  calls++;
  return result;
}
}  // namespace timezone_lookup
namespace filetransfer {
inline int owners = 0;
inline bool acquire() {
  owners++;
  return true;
}
inline void release() {
  if (owners) owners--;
}
}  // namespace filetransfer

constexpr int ESP_OK = 0;
constexpr int ESP_MAC_WIFI_STA = 0;
using esp_err_t = int;
inline esp_err_t esp_read_mac(uint8_t* mac, int) {
  std::fill(mac, mac + 6, 1);
  return ESP_OK;
}
inline const char* defaultNetworkName() { return "tenor-cross"; }
inline void deviceNetworkName(char* out, size_t size, const char* fallback) { std::snprintf(out, size, "%s", fallback); }
struct FakeEsp {
  unsigned getFreeHeap() const { return 100000; }
  unsigned getMaxAllocHeap() const { return 50000; }
};
inline FakeEsp ESP;

class UITheme {
 public:
  enum class TextVerticalAlignment { BOTTOM };
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  ThemeMetrics getMetrics() const { return {}; }
  Rect getScreenSafeArea(GfxRenderer&, bool, bool) const { return {}; }
  static void drawCenteredText(GfxRenderer&, const Rect&, int, int, const char*, bool = false,
                               EpdFontFamily = EpdFontFamily::BOLD) {}
  static void drawCenteredWrappedText(GfxRenderer&, const Rect&, int, const char*, int, bool = false,
                                      EpdFontFamily = EpdFontFamily::BOLD,
                                      TextVerticalAlignment = TextVerticalAlignment::BOTTOM) {}
};
struct FakeGui {
  std::string lastButton2;
  void drawHeader(GfxRenderer&, Rect, const char*, const char*) {}
  void drawSubHeader(GfxRenderer&, Rect, const char*) {}
  void drawHelpText(GfxRenderer&, Rect, const char*) {}
  void drawButtonHints(GfxRenderer&, const char*, const char* b, const char*, const char*) { lastButton2 = b ? b : ""; }
};
inline FakeGui GUI;
