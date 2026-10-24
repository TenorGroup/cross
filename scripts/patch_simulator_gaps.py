"""Fill the gaps the firmware grew past the pinned simulator facade (v1.0.66 to v1.0.73).

- BoardConfig::hasImu (v1.0.66): the simulated boards have no motion sensor, as HalTiltSensor says.
- WiFiClass::hostByName (v1.0.73): the font store checks its host resolves; the host resolver answers.
- WebServer::client() by reference and NetworkClient::write(const char*, size_t) (v1.0.69): the file
  listing writes its chunks straight to the client, as the ESP32 core allows.

Same contract as patch_simulator_grayscale.py: exactly one match per edit, skip an edit already
applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
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

for name, old, new in EDITS:
    path = src / name
    source = path.read_text()
    if new in source:
        continue
    if source.count(old) != 1:
        raise RuntimeError(f"Simulator {name} changed; review patch_simulator_gaps.py before building")
    path.write_text(source.replace(old, new, 1))
    print(f"Filled a simulator gap in {name}")
