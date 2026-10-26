"""Fill the gaps the firmware grew past the pinned simulator facade (v1.0.66 to v1.0.73).

- BoardConfig::hasImu (v1.0.66): the simulated boards have no motion sensor, as HalTiltSensor says.
- WiFiClass::hostByName (v1.0.73): the font store checks its host resolves; the host resolver answers.
- esp_random.h and HalHaptics.h (v1.1.0): the firmware includes both; the facade has neither (no radio, no motor).
- HalFile::truncate (v1.1.0): the clipping deletion journal cuts a torn tail; ftruncate does the same.
- HalGPIO::wasCapacitivePagePressed / isCapacitivePagePressed (v1.1.0): no simulated board has page pads.
- HalStorage::readFileToString / replaceFile, HalGPIO::hasToggleSwitch / isToggleSwitchOn, BoardConfig::isEegoA4 /
  isM5PaperV11 / isMetalioEInk4 / isPicco, esp_wifi_get_ps (v1.1.0): plugin catalog, slide switch, board checks, Wi-Fi guard.
- SecureHttpClient std::string headers, streamed sendRequest, responseComplete (v1.1.0): clipping sync; connect
  (CrossPoint #4167): plugin request headers.
- BoardProfile corner fields and the FREEINK_DEVICE_PICCO / EEGO_A4 / METALIO_EINK4 defaults (v1.1.0): theme layout and
  the power-wake test read them. (HalClock::begin(bool) and rtcEpoch are in patch_simulator_clock.py.)
- ResumableFetch.h (v1.1.0): the plugin fetch in the web server; SecureNet is ignored here, so the fetch reports no route.
- wolfssl/wolfcrypt/aes.h and hash.h (v1.1.0): the web server's crypto endpoint, plugin device ID and book-key wrap;
  refusing stand-ins.
- xSemaphoreCreateMutexStatic / StaticSemaphore_t (v1.1.0): Epub's GIF cover lock.
- String::toLowerCase / reserve / begin / end, WebServer's retained-argument members, SecureHttpClient::setUserAgent
  (v1.1.0): plugin endpoints in the web server.
- nvs blobs, Preferences.h (v1.1.0): the device secret and the trusted-time floor, in memory here.
- Arduino.h pulling in FreeRTOS.h and sys/time.h (v1.1.0), as the ESP32 core does: TrustedTime's spinlock and settimeofday.
- WolfsslCrypto (v1.1.0): the LCP and protected-book glue names the wolfSSL backend; the simulator has no wolfSSL
  (and no TLS to reach an LCP license server): scripts/simulator_stubs/WolfsslCrypto.h stands in, force-included, see below.
- WebServer::client() by reference and NetworkClient::write(const char*, size_t) (v1.0.69): the file
  listing writes its chunks straight to the client, as the ESP32 core allows.

Same contract as patch_simulator_grayscale.py: exactly one match per edit, skip an edit already
applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
# WolfsslCrypto without wolfSSL: the SDK header is empty unless FREEINK_CONTENT_WOLFSSL is set, and no include
# path can outrank it (lib paths come before build_flags -I), so the stand-in is force-included in C++ units.
env.Append(CXXFLAGS=["-include", str(Path(env["PROJECT_DIR"]) / "scripts" / "simulator_stubs" / "WolfsslCrypto.h")])
src = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src"

EDITS = (
    ("BoardConfig.h", "inline void holdPowerRails() {}\n",
     "inline void holdPowerRails() {}\n\n"
     "// No simulated board carries a motion sensor (HalTiltSensor reports none).\n"
     "inline bool hasImu() { return false; }\n"),
    ("WiFi.h", "class WiFiClass {\n",
     "#include <netdb.h>\n#include <netinet/in.h>\n\n"
     "class WiFiClass {\n"
     "public:\n"
     "  // The host's resolver answers, as lwIP's would on the device: 1 found, 0 not.\n"
     "  int hostByName(const char *host, IPAddress &result) {\n"
     "    addrinfo hints{};\n"
     "    hints.ai_family = AF_INET;\n"
     "    addrinfo *found = nullptr;\n"
     "    if (!host || getaddrinfo(host, nullptr, &hints, &found) != 0 || !found) return 0;\n"
     "    const uint32_t address = reinterpret_cast<sockaddr_in *>(found->ai_addr)->sin_addr.s_addr;\n"
     "    freeaddrinfo(found);\n"
     "    const auto *bytes = reinterpret_cast<const uint8_t *>(&address);\n"
     "    result = IPAddress(bytes[0], bytes[1], bytes[2], bytes[3]);\n"
     "    return 1;\n"
     "  }\n\n"
     "private:\n"),
    ("NetworkClient.h", "  virtual size_t write(uint8_t c) { return write(&c, 1); }\n",
     "  virtual size_t write(uint8_t c) { return write(&c, 1); }\n"
     "  size_t write(const char *buf, size_t size) {\n"
     "    return write(reinterpret_cast<const uint8_t *>(buf), size);\n"
     "  }\n"),
    ("HalStorage.h", "  bool sync();\n", "  bool sync();\n  bool truncate(uint64_t length);\n"),
    ("HalStorage.cpp", "size_t HalFile::fileSize() { return size(); }\n",
     "size_t HalFile::fileSize() { return size(); }\n"
     "bool HalFile::truncate(uint64_t length) {\n"
     "  return impl && impl->fd >= 0 && ftruncate(impl->fd, static_cast<off_t>(length)) == 0;\n"
     "}\n"),
    ("HalStorage.h", "  bool writeFile(const char *path, const String &content);\n",
     "  bool writeFile(const char *path, const String &content);\n"
     "  bool readFileToString(const char *moduleName, const std::string &path, size_t cap, std::string &out);\n"
     "  bool replaceFile(const char *tmpPath, const char *path);\n"),
    ("HalStorage.cpp", "bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path); }\n",
     "bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path); }\n"
     "// Whole file into `out`; fails on missing, directory, empty or above-cap files, as the device does.\n"
     "bool HalStorage::readFileToString(const char *, const std::string &path, size_t cap, std::string &out) {\n"
     "  out.clear();\n"
     "  HalFile f = open(path.c_str(), O_RDONLY);\n"
     "  if (!f || f.isDirectory()) return false;\n"
     "  const size_t size = f.size();\n"
     "  if (size == 0 || size > cap) return false;\n"
     "  out.resize(size);\n"
     "  return f.read(out.data(), size) == static_cast<int>(size);\n"
     "}\n"
     "// rename(2) replaces the target, as the device's commit does.\n"
     "bool HalStorage::replaceFile(const char *tmpPath, const char *path) { return rename(tmpPath, path); }\n"),
    ("HalGPIO.h", "  bool wasAnyPressed() const;\n",
     "  bool wasAnyPressed() const;\n"
     "  // No simulated board has capacitive page pads or a slide switch.\n"
     "  bool wasCapacitivePagePressed() const { return false; }\n"
     "  bool isCapacitivePagePressed(uint8_t) const { return false; }\n"
     "  bool hasToggleSwitch() const { return false; }\n"
     "  bool isToggleSwitchOn() const { return false; }\n"),
    ("BoardConfig.h", "inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }\n",
     "inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }\n"
     "// Boards the simulator does not model.\n"
     "inline bool isEegoA4() { return false; }\n"
     "inline bool isM5PaperV11() { return false; }\n"
     "inline bool isMetalioEInk4() { return false; }\n"
     "inline bool isPicco() { return false; }\n"),
    ("esp_wifi.h", "inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }\n",
     "inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }\n"
     "inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type) {\n"
     "  *type = WIFI_PS_NONE;\n"
     "  return ESP_OK;\n"
     "}\n"),
    ("SecureHttpClient.h", "  String getString() { return http_.getString(); }\n",
     "  String getString() { return http_.getString(); }\n\n"
     "  // The SDK client's std::string headers and streamed body; the simulator buffers the whole answer, then\n"
     "  // hands it to the callback in one piece.\n"
     "  using DataCallback = std::function<bool(const uint8_t *data, size_t len)>;\n"
     "  void addHeader(const std::string &name, const std::string &value) { http_.addHeader(name.c_str(), value.c_str()); }\n"
     "  int sendRequest(const char *method, const uint8_t *payload, size_t payloadLen, const DataCallback &onData,\n"
     "                  const std::function<bool()> & = nullptr) {\n"
     "    const std::string body = payload ? std::string(reinterpret_cast<const char *>(payload), payloadLen) : std::string();\n"
     "    const int status = sendRequest(method, String(body));\n"
     "    const String answer = http_.getString();\n"
     "    complete_ = status > 0 && onData(reinterpret_cast<const uint8_t *>(answer.c_str()), answer.length());\n"
     "    return status;\n"
     "  }\n"
     "  bool responseComplete() const { return complete_; }\n"
     "  // The SDK handshakes ahead so request headers do not take the heap TLS needs; the simulator has no TLS.\n"
     "  bool connect(const std::function<bool()> & = nullptr) { return true; }\n"
     "  // The simulator's HTTP client keeps no response headers.\n"
     "  std::vector<std::pair<std::string, std::string>> getHeaders() const { return {}; }\n"),
    ("SecureHttpClient.h", "private:\n  NetworkClientSecure client_;\n",
     "private:\n  bool complete_ = false;\n  NetworkClientSecure client_;\n"),
    ("SecureHttpClient.h", "#include <string>\n", "#include <functional>\n#include <string>\n#include <utility>\n#include <vector>\n"),
    ("BoardConfig.h", "  BatteryGaugeConfig batteryGauge = {};\n};\n",
     "  BatteryGaugeConfig batteryGauge = {};\n"
     "  // Rounded-corner layout of the boards that have it; the simulated panels are square.\n"
     "  int8_t headerOffsetY = 0;\n"
     "  uint8_t cornerRadius = 0;\n"
     "  int8_t statusBarOffsetY = 0;\n"
     "  uint8_t bottomCornerRadius = 0;\n"
     "};\n"),
    ("BoardConfig.h", "#include <cstdint>\n\n// Keep the small portion",
     "#include <cstdint>\n\n"
     "// Device flags the firmware reads inside expressions; the SDK header defaults them to 0, as here.\n"
     "#ifndef FREEINK_DEVICE_PICCO\n#define FREEINK_DEVICE_PICCO 0\n#endif\n"
     "#ifndef FREEINK_DEVICE_EEGO_A4\n#define FREEINK_DEVICE_EEGO_A4 0\n#endif\n"
     "#ifndef FREEINK_DEVICE_METALIO_EINK4\n#define FREEINK_DEVICE_METALIO_EINK4 0\n#endif\n\n"
     "// Keep the small portion"),
    ("WString.h", "  bool isEmpty() const { return s.empty(); }\n",
     "  bool isEmpty() const { return s.empty(); }\n"
     "  void toLowerCase() {\n"
     "    for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));\n"
     "  }\n"
     "  bool reserve(unsigned int size) {\n"
     "    s.reserve(size);\n"
     "    return true;\n"
     "  }\n"
     "  void remove(unsigned int index) {\n"
     "    if (index < s.length()) s.erase(index);\n"
     "  }\n"
     "  const char *begin() const { return s.c_str(); }\n"
     "  const char *end() const { return s.c_str() + s.length(); }\n"),
    ("WebServer.h", "class WebServer {\npublic:\n",
     "class WebServer {\n"
     "protected:\n"
     "  // The ESP32 core keeps parsed request arguments here; the simulator's server keeps none, and the\n"
     "  // firmware's CrossPointHttpServer releases them between requests.\n"
     "  struct SimRequestArgument {};\n"
     "  SimRequestArgument *_currentArgs = nullptr;\n"
     "  int _currentArgCount = 0;\n"
     "  SimRequestArgument *_postArgs = nullptr;\n"
     "  int _postArgsLen = 0;\n\n"
     "public:\n"),
    ("SecureHttpClient.h", "  void setTimeout(uint16_t ms) { http_.setTimeout(ms); }\n",
     "  void setTimeout(uint16_t ms) { http_.setTimeout(ms); }\n  void setUserAgent(const char *) {}\n"),
    ("nvs.h", "inline void nvs_close(nvs_handle_t /*handle*/) {}\n",
     "inline void nvs_close(nvs_handle_t /*handle*/) {}\n\n"
     "// Blobs: nothing is stored between runs, so a missing blob is created anew (the device secret, per run).\n"
     "constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;\n"
     "inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *) { return ESP_ERR_NVS_NOT_FOUND; }\n"
     "inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t) { return ESP_OK; }\n"),
    ("Arduino.h", "#include <thread>\n",
     "#include <thread>\n"
     "// The ESP32 core's Arduino.h brings these in; TrustedTime reads portMUX_TYPE and settimeofday through it.\n"
     "#include <sys/time.h>\n\n"
     "#include \"freertos/FreeRTOS.h\"\n"),
    ("freertos/semphr.h", "inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new SimMutex(); }\n",
     "inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new SimMutex(); }\n\n"
     "// The caller's storage is for FreeRTOS's static allocation; the simulator allocates its own.\n"
     "struct StaticSemaphore_t {\n  int unused;\n};\n"
     "inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *) { return new SimMutex(); }\n"),
    ("WebServer.h", "  NetworkClient client();\n", "  NetworkClient &client();\n"),
    ("WebServer.cpp", "#include <thread>\n",
     "#include <thread>\n\n"
     "// The client a handler writes to (client() by reference); dropped with the request, so the peer sees the\n"
     "// end of the body when the server closes the socket.\n"
     "static NetworkClient &simHeldClient() {\n"
     "  static NetworkClient held;\n"
     "  return held;\n"
     "}\n"),
    ("WebServer.cpp", "  void resetRequest() {\n    currentClient = -1;\n",
     "  void resetRequest() {\n    simHeldClient() = NetworkClient();\n    currentClient = -1;\n"),
    ("WebServer.cpp",
     "NetworkClient WebServer::client() {\n"
     "  if (impl_->currentClient < 0)\n"
     "    return NetworkClient(-1);\n"
     "  const int fd = ::dup(impl_->currentClient);\n"
     "  if (fd < 0)\n"
     "    LOG_ERR(\"WEB\", \"[SIM] Failed to duplicate client socket: %s\",\n"
     "            strerror(errno));\n"
     "  return NetworkClient(fd);\n"
     "}\n",
     "NetworkClient &WebServer::client() {\n"
     "  NetworkClient &held = simHeldClient();\n"
     "  if (impl_->currentClient < 0) {\n"
     "    held = NetworkClient(-1);\n"
     "    return held;\n"
     "  }\n"
     "  const int fd = ::dup(impl_->currentClient);\n"
     "  if (fd < 0)\n"
     "    LOG_ERR(\"WEB\", \"[SIM] Failed to duplicate client socket: %s\",\n"
     "            strerror(errno));\n"
     "  held = NetworkClient(fd);\n"
     "  return held;\n"
     "}\n"),
)

# New headers: lib/hal is ignored by the simulator env, so the facade carries these stand-ins.
NEW_FILES = {
    "esp_random.h": (
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n\n"
        "// The host's entropy, as the ESP32 hardware RNG would answer.\n"
        "inline uint32_t esp_random() { return static_cast<uint32_t>(arc4random()); }\n"
        "inline void esp_fill_random(void *buffer, size_t size) { arc4random_buf(buffer, size); }\n"),
    "ResumableFetch.h": (
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n#include <functional>\n#include <string>\n\n"
        "#include \"SecureHttpClient.h\"\n\n"
        "// SecureNet is ignored in the simulator (no HTTPS), so the SDK's resumable GET is not there: this one keeps\n"
        "// its types and reports a transport failure (status -1), as the device does with no route to the host.\n"
        "namespace freeink {\n"
        "struct FetchOptions {\n  size_t startOffset = 0;\n  bool redirectToHttp = false;\n};\n"
        "struct FetchSink {\n"
        "  std::function<bool(const uint8_t *data, size_t len)> write;\n"
        "  std::function<bool()> rewind;\n"
        "  std::function<void(size_t bytes, size_t total)> progress;\n};\n"
        "struct FetchResult {\n  int status = 0;\n  size_t bytes = 0;\n  size_t total = 0;\n  bool complete = false;\n"
        "  bool stopped = false;\n  bool aborted = false;\n};\n"
        "inline FetchResult fetchResumable(const std::string &, const FetchOptions &options,\n"
        "                                  const std::function<void(SecureHttpClient &, bool)> &, const FetchSink &,\n"
        "                                  const std::function<bool()> & = nullptr) {\n"
        "  FetchResult result;\n  result.status = -1;\n  result.bytes = options.startOffset;\n  return result;\n}\n"
        "}  // namespace freeink\n"),
    "wolfssl/wolfcrypt/aes.h": (
        "#pragma once\n#include <cstdint>\n\n"
        "// The web server's crypto endpoint and plugin device ID name a few wolfCrypt calls; the simulator links no\n"
        "// wolfSSL, so they refuse (the endpoint answers 'failed', the device ID is left out).\n"
        "struct Aes {\n  int unused;\n};\n"
        "constexpr int AES_ENCRYPTION = 0;\nconstexpr int AES_DECRYPTION = 1;\nconstexpr int INVALID_DEVID = -2;\n"
        "inline int wc_AesInit(Aes *, void *, int) { return -1; }\n"
        "inline void wc_AesFree(Aes *) {}\n"
        "inline int wc_AesGcmSetKey(Aes *, const uint8_t *, uint32_t) { return -1; }\n"
        "inline int wc_AesGcmEncrypt(Aes *, uint8_t *, const uint8_t *, uint32_t, const uint8_t *, uint32_t, uint8_t *,\n"
        "                            uint32_t, const uint8_t *, uint32_t) { return -1; }\n"
        "inline int wc_AesGcmDecrypt(Aes *, uint8_t *, const uint8_t *, uint32_t, const uint8_t *, uint32_t,\n"
        "                            const uint8_t *, uint32_t, const uint8_t *, uint32_t) { return -1; }\n"
        "inline int wc_AesSetKey(Aes *, const uint8_t *, uint32_t, const uint8_t *, int) { return -1; }\n"
        "inline int wc_AesCbcEncrypt(Aes *, uint8_t *, const uint8_t *, uint32_t) { return -1; }\n"
        "inline int wc_AesCbcDecrypt(Aes *, uint8_t *, const uint8_t *, uint32_t) { return -1; }\n"),
    "wolfssl/wolfcrypt/hash.h": (
        "#pragma once\n#include <cstdint>\n\n"
        "inline int wc_Sha256Hash(const uint8_t *, uint32_t, uint8_t *) { return -1; }\n"),
    "Preferences.h": (
        "#pragma once\n#include <cstdint>\n#include <map>\n#include <string>\n\n"
        "// Key-value store of the ESP32 core, kept in memory: a run starts as a first boot, so the trusted-time floor\n"
        "// never vetoes a pinned CROSSPOINT_SIM_NOW.\n"
        "class Preferences {\n public:\n"
        "  bool begin(const char *name, bool = false, const char * = nullptr) {\n    ns_ = name ? name : \"\";\n    return true;\n  }\n"
        "  void end() {}\n"
        "  int64_t getLong64(const char *key, int64_t fallback = 0) {\n"
        "    const auto found = store().find(ns_ + '/' + key);\n    return found == store().end() ? fallback : found->second;\n  }\n"
        "  size_t putLong64(const char *key, int64_t value) {\n    store()[ns_ + '/' + key] = value;\n    return 8;\n  }\n"
        "  uint8_t getUChar(const char *key, uint8_t fallback = 0) { return static_cast<uint8_t>(getLong64(key, fallback)); }\n"
        "  size_t putUChar(const char *key, uint8_t value) {\n    putLong64(key, value);\n    return 1;\n  }\n\n"
        " private:\n"
        "  static std::map<std::string, int64_t> &store() {\n    static std::map<std::string, int64_t> values;\n    return values;\n  }\n"
        "  std::string ns_;\n};\n"),
    "HalHaptics.h": (
        "#pragma once\n#include <cstdint>\n\n"
        "// No simulated board carries a motor: the feedback policy calls do nothing.\n"
        "class HalHaptics {\n public:\n"
        "  static void longPress(bool, uint8_t) {}\n"
        "  static void feedback(bool, bool, uint8_t) {}\n};\n"),
}
for name, text in NEW_FILES.items():
    path = src / name
    if not path.exists() or path.read_text() != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        print(f"Added a simulator stand-in {name}")

for name, old, new in EDITS:
    path = src / name
    source = path.read_text()
    if new in source:
        continue
    if source.count(old) != 1:
        raise RuntimeError(f"Simulator {name} changed; review patch_simulator_gaps.py before building")
    path.write_text(source.replace(old, new, 1))
    print(f"Filled a simulator gap in {name}")
