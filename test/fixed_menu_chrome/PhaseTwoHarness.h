#include <cstring>
#include <initializer_list>
#include <set>

namespace tenorchrome {
std::vector<X3ScreenRect> phaseRings;
std::vector<X3ScreenRect> phaseBars;
void drawPillRing(const GfxRenderer&, int x, int y, int width, int height, int, bool) {
  phaseRings.push_back({x, y, width, height});
}
}

// PHASE_TWO_ADAPTER
using MappedInputManager = HomeActivity::InputBoundary;

// PHASE_TWO_POPUP

struct Rect {
  int x, y, width, height;
};
namespace freeink { namespace ui {
struct Insets { int16_t top, right, bottom, left; };
struct Rect { int16_t x, y, width, height; };
}}
namespace fui = freeink::ui;
struct UiScreen {
  fui::Insets margins{};
  void setContentMarginFromScreen(fui::Insets value) { margins = value; }
};
namespace QrUtils {
std::vector<Rect> bounds;
void drawQrCode(const GfxRenderer&, Rect rect, const std::string&) { bounds.push_back(rect); }
}
namespace menutilt {
struct Step { bool next = false, previous = false; };
Step pending;
int polls = 0;
void pollTabs(uint8_t, bool) {}
Step pollRows(bool) { ++polls; const auto result = pending; pending = {}; return result; }
}
enum class Mode { SIGN_UP, AUTHENTICATE };
enum class InputType { Text, Password, Url };
enum class OpdsEntryType { BOOK, NAVIGATION };
// PHASE_TWO_STATES
constexpr int CHART_BAR_MAX = 36;
namespace lich30 { constexpr int SO_TUAN = 4; }
constexpr uint8_t kUnassigned = 255;
constexpr uint8_t kRoleCount = 4;
struct CrossPointSettings {
  static constexpr uint8_t FRONT_HW_BACK = 0, FRONT_HW_CONFIRM = 1, FRONT_HW_LEFT = 2, FRONT_HW_RIGHT = 3;
};
constexpr const char* CROSSPOINT_VERSION = "v1.0.56";
unsigned long millis() { return 1000; }
const char* apHostname() { return "tenor"; }
struct WifiBoundary {
  struct Ip { std::string toString() const { return "192.168.1.1"; } };
  Ip localIP() const { return {}; }
} WiFi;
struct PositionBoundary { int pageNumber = 1, totalPages = 12, spineIndex = 0; };
struct ProgressBoundary { float percentage = .5f; std::string device = "remote device"; };
struct EpubBoundary {
  int getTocIndexForSpineIndex(int) const { return 0; }
  struct Toc { std::string title = "Remote chapter"; };
  Toc getTocItem(int) const { return {}; }
};
struct UpdaterBoundary {
  std::string getLatestVersion() const { return "v1.0.57"; }
  unsigned getProcessedSize() const { return 100; }
  unsigned getTotalSize() const { return 200; }
};
struct PopupBoundary {
  bool processRender(const GfxRenderer&, const HomeActivity::InputBoundary&) const { return false; }
};
struct NetworkBoundary { bool hasSavedPassword = true; };
struct EntryBoundary { OpdsEntryType type = OpdsEntryType::BOOK; };
struct ServerBoundary { std::string name = "OPDS"; };
struct PhaseActivityBoundary {
  GfxRenderer& renderer;
  explicit PhaseActivityBoundary(GfxRenderer& value) : renderer(value) {}
  HomeActivity::InputBoundary mappedInput;
  x3activity::InfoPages x3Info;
  x3activity::TwoChoicePopup x3Popup;
  std::string navigationPrefix = "Home/Settings";
  int state = 0, currentStep = 0, cursor = 2, selectorIndex = 0, selectedOption = 0, x3Step = 0, selected = 0;
  int awarded = 0;
  int lastRenderedPercent = -1, lastUpdaterPercentage = 50, selectedNetworkIndex = 0, realNetworkCount = 1;
  Mode mode = Mode::AUTHENTICATE;
  InputType inputType = InputType::Text;
  bool recoveryMode = false, isApMode = false, autoConnecting = false, deferManualNetworkList = false, grid = true, readable = true, failed = true;
  bool cursorMode = false, urlPanel = false, symbols = false, shifted = false, passwordVisible = false;
  bool firstProgressFrame = false;
  bool togglePos = false;
  unsigned long lastProgressReceived = 100, lastProgressTotal = 200, lastCompleteAt = 1, writtenBytes = 100, firmwareSize = 200;
  unsigned long downloadProgress = 100, downloadTotal = 200;
  uint32_t weekMinutes[4] = {0, 12, 80, 100}, weekKey[4] = {20260901, 20260908, 20260915, 20260922};
  bool weekHasSubMinute[4] = {true, false, false, false};
  uint32_t firstKey = 20260909, lastKey = 20261009;
  uint8_t tempMapping[4] = {0, 255, 255, 255};
  char digits[7] = "000123", syncedTime[16] = "12:34";
  const char* failedDetail = "Update failed detail";
  const char* status = "Status", *evidence = "Reading evidence";
  std::string title = "Keyboard", panicMessage = "panic detail", statusMessage = "Status detail", errorMessage = "Error detail";
  std::string connectedSSID = "Network", selectedSSID = "Network", connectedIP = "192.168.1.1", cachedMacAddress = "AA:BB:CC:DD:EE:FF";
  std::string connectionError = "Connection detail", currentUploadName = "book.epub", lastCompleteName = "book.epub";
  std::string localChapterName = "Local chapter", textPayload = "QR payload", searchTemplate = "search";
  std::string statusText_ = "BLE status", bindNotice_ = "Binding notice";
  std::vector<NetworkBoundary> networks{{}};
  std::vector<EntryBoundary> entries{{}};
  ServerBoundary server;
  EpubBoundary epubObject;
  EpubBoundary* epub = &epubObject;
  PositionBoundary remotePosition, localPosition;
  ProgressBoundary remoteProgress, localProgress;
  UpdaterBoundary updater;
  PopupBoundary confirmPopup;
  int listNav = 0, uiRenders = 0;
  int updates = 0, drained = 0;
  int listTilts = 0;
  bool idleExpired = false;
  int idleChecks = 0, finished = 0;
  bool idleExitDue(unsigned long, bool) { ++idleChecks; return idleExpired; }
  void finish() { ++finished; }
  void requestUpdate() { ++updates; }
  void readPendingKeys() { ++drained; }
  const char* getRoleName(int) const { return "Role"; }
  const char* getHardwareName(int) const { return "Button"; }
  const char* headerTitle() const { return "Screen"; }
  void renderUi() { ++uiRenders; }
  void drawGrid() {}
  void drawChart() {}
  int gridTop() const { return tenorchrome::x3ScreenLayout(renderer).bodyRect.y + 4; }
  int gridHeight() const { return 3 * ((renderer.getScreenWidth() - 48) / 10) + renderer.getLineHeight(SMALL_FONT_ID) + 12; }
  int chartHeight() const { return 4 * renderer.getLineHeight(SMALL_FONT_ID) + 46; }
  struct Summary { int coverage = 1; } summary;
  template <typename Draw> void renderSettledList(int, Draw draw) { draw(); }
};

// PHASE_TWO_CLASSES
