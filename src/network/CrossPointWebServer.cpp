#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <InlineButtonText.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"
#include "DeviceName.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WebPathPolicy.h"
#include "WifiCredentialStore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/ThemeCss.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
unsigned long wsLastActivityTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) {
  if (!web_path::allowed(std::string_view(inputPath.c_str(), inputPath.length()))) return "";
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedItemName(const String& name) {
  return !web_path::allowed(std::string_view(name.c_str(), name.length()));
}

// Locale-aware lookup for the web transfer surface: one request's locale
// without touching the global I18N singleton (the device language must not
// change while a web request is being served).
inline const char* trWeb(Language language, StrId id) { return I18n::getInstance().get(id, language); }

// Error body for the font endpoints, whose responses are JSON. The key text is
// appended (no intermediate format buffer); quotes and backslashes are escaped
// so no translation can produce an invalid payload.
String webErrorBody(Language language, StrId id) {
  String body = "{\"error\":\"";
  for (const char* c = trWeb(language, id); *c != '\0'; ++c) {
    if (*c == '"' || *c == '\\') body += '\\';
    body += *c;
  }
  body += "\"}";
  return body;
}

}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {}

Language CrossPointWebServer::requestLanguage() const {
  // Contract: the request's own `lang` query argument decides the locale;
  // anything missing or unrecognised falls back to Vietnamese.
  if (!server) return Language::VI;
  const String code = server->arg("lang");
  if (code == "en-AU") return Language::EN;
  if (code == "zh-Hans") return Language::ZH_HANS;
  return Language::VI;
}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;
  char hostname[64];
  deviceNetworkName(hostname, sizeof(hostname), "tenor-cross");
#ifdef SIMULATOR
  auth.configure("127.0.0.1", hostname);
#else
  auth.configure(apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(), hostname);
#endif

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  server->enableCORS(false);

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleRoot();
  });
  server->on("/files", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleFileList();
  });
  server->on("/theme.css", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleTheme();
  });
  server->on("/js/jszip.min.js", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleJszip();
  });

  server->on("/api/status", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleStatus();
  });
  server->on("/api/files", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleFileListData();
  });
  server->on("/download", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleDownload();
  });

  // Upload endpoint with special handling for multipart form data
  server->on(
      "/upload", HTTP_POST,
      [this] {
        if (auth.authorize(*server, true, requestLanguage())) handleUploadPost(upload);
      },
      [this] {
        if (auth.authorize(*server, false, requestLanguage())) handleUpload(upload);
      });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleCreateFolder();
  });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleRename();
  });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleMove();
  });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleDelete();
  });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleSettingsPage();
  });
  server->on("/api/settings", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleGetSettings();
  });
  server->on("/api/settings", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handlePostSettings();
  });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleFontsPage();
  });
  server->on("/api/fonts", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleFontList();
  });
  server->on(
      "/api/fonts/upload", HTTP_POST,
      [this] {
        if (auth.authorize(*server, true, requestLanguage())) handleFontUpload();
      },
      [this] {
        if (auth.authorize(*server, false, requestLanguage())) handleFontUploadData();
      });
  server->on("/api/fonts/delete", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleFontDelete();
  });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleGetOpdsServers();
  });
  server->on("/api/opds", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handlePostOpdsServer();
  });
  server->on("/api/opds/delete", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleDeleteOpdsServer();
  });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleGetWifiNetworks();
  });
  server->on("/api/wifi", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handlePostWifiNetwork();
  });
  server->on("/api/wifi/delete", HTTP_POST, [this] {
    if (auth.authorize(*server, true, requestLanguage())) handleDeleteWifiNetwork();
  });

  server->on("/api/session", HTTP_GET, [this] {
    if (!auth.authorize(*server, true, requestLanguage())) return;
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "text/plain", auth.token());
  });

  server->onNotFound([this] {
    if (auth.authorize(*server, true, requestLanguage())) handleNotFound();
  });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  // If-None-Match is collected so the static-page handlers can answer conditional GETs with 304
  const char* collectedHeaders[] = {"Depth",   "Destination",   "Overwrite", "If",     "Lock-Token",
                                    "Timeout", "If-None-Match", "Host",      "Origin", "Authorization"};
  server->collectHeaders(collectedHeaders, 10);
  server->addHandler(
      new WebDAVHandler(auth));  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  const char* requiredHeaders[] = {"Origin"};
  wsServer->onValidateHttpHeader(
      [this](String name, String value) {
        return !web_path::equalsFolded(name.c_str(), "Origin") || auth.originAllowed(value.c_str());
      },
      requiredHeaders, 1);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  // Do not subscribe the serving task to the task watchdog. Arduino WebServer
  // permits five-second client and ACK waits, which can consume the entire
  // default watchdog window on a weak connection. The interrupt watchdog still
  // catches hard CPU lockups, matching the rest of the application lifecycle.

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  // Explicit close() required: file-scope global persists beyond function scope
  wsUploadFile.close();
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  if (Storage.remove(filePath.c_str())) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_INF("WEB", "Alive port=%u heap=%u largest=%u stack=%u wifi=%d rssi=%d", port, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap(), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
            static_cast<int>(WiFi.status()), WiFi.RSSI());
    lastDebugPrint = millis();
  }

  if (wsUploadInProgress && millis() - wsLastActivityTime > 30000) {
    const auto owner = wsUploadClientNum;
    abortWsUpload("WS");
    if (wsServer) wsServer->disconnect(owner);
  }
  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendStaticContent(WebServer* server, const char* data, size_t len, const char* etag,
                              const char* contentType) {
  // Content is baked into flash at build time, so the ETag is stable for the
  // lifetime of a firmware image. Honor If-None-Match with a 304 so browsers
  // reuse their cache instead of re-downloading on every navigation.
  if (server->header("If-None-Match") == etag) {
    server->sendHeader("ETag", etag);
    server->sendHeader("Cache-Control", "no-cache");
    server->send(304);
    return;
  }
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("ETag", etag);
  // no-cache: the browser may cache, but must revalidate (conditional GET)
  // before reuse - this is what unlocks 304 responses.
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, contentType, data, len);
}

void CrossPointWebServer::handleTheme() const {
  sendStaticContent(server.get(), ThemeCss, ThemeCssCompressedSize, ThemeCssETag, "text/css; charset=utf-8");
}

void CrossPointWebServer::handleRoot() const {
  sendStaticContent(server.get(), HomePageHtml, sizeof(HomePageHtml), HomePageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  sendStaticContent(server.get(), jszip_minJs, jszip_minJsCompressedSize, jszip_minJsETag, "application/javascript");
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const {
  const Language lang = requestLanguage();
  // CORS preflight: routes are registered per-method, so OPTIONS requests land
  // here. Only same-origin browser requests are allowed by the auth gate.
  if (server->method() == HTTP_OPTIONS) {
    server->send(204, "text/plain", "");
    return;
  }

  // in AP mode, redirect unmatched browser/captive-portal requests to "/" so the OS auto-opens the browser
  // API requests (/api/*) still return 404 so XHR errors surface correctly
  // see https://en.wikipedia.org/wiki/Captive_portal#Detection
  if (apMode && !server->uri().startsWith("/api/")) {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
    return;
  }

  String message = trWeb(lang, StrId::STR_WEB_NOT_FOUND);
  message += "\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  const Language lang = requestLanguage();
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
#else
  doc["device"] = BoardConfig::ACTIVE.name;
#endif

  char snBuf[33] = {0};
  bool valid = false;
#if !CONFIG_IDF_TARGET_ESP32
  // Classic ESP32's efuse table has no USER_DATA block (C3/S3 only)
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, snBuf, 256) == ESP_OK) {
    valid = snBuf[0] != '\0' && snBuf[0] != (char)0xFF;
    for (int i = 0; i < 32 && snBuf[i] != '\0'; i++) {
      if (!std::isprint(static_cast<unsigned char>(snBuf[i]))) {
        valid = false;
        break;
      }
    }
  }
#endif

  if (valid) {
    doc["serial"] = snBuf;
  } else {
    doc["serial"] = trWeb(lang, StrId::STR_WEB_SERIAL_NOT_FOUND);
  }

  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = isProtectedItemName(fileName);

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();                          // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  sendStaticContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml), FilesPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleFileListData() const {
  const Language lang = requestLanguage();
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }

  if (currentPath.isEmpty()) {
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_PROTECTED_PATH));
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleDownload() {
  const Language lang = requestLanguage();
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_PATH));
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_PATH));
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", trWeb(lang, StrId::STR_WEB_ITEM_NOT_FOUND));
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_FAILED_OPEN_FILE));
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_PATH_IS_DIRECTORY));
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  // HTTP handlers run serially; reuse the upload arena instead of spending
  // half of loopTask's stack on a second transfer buffer.
  auto* buffer = upload.buffer.data();
  const size_t chunkSize = upload.buffer.size();
  const uint32_t started = millis();
  size_t sent = 0;
  LOG_INF("WEB", "Download begin bytes=%u heap=%u stack=%u", static_cast<unsigned>(file.size()), ESP.getFreeHeap(),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  bool downloadOk = chunkSize > 0;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
      sent += wrote;
    }
  }
  client.clear();
  file.close();
  LOG_INF("WEB", "Download end ok=%d sent=%u elapsed=%lu heap=%u stack=%u", downloadOk, static_cast<unsigned>(sent),
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap(),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  const Language lang = requestLanguage();
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.fileName = upload.filename;
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    if (!FsHelpers::isSafePathComponent(state.fileName) || isProtectedItemName(state.fileName)) {
      state.error = trWeb(lang, StrId::STR_WEB_INVALID_FILE_NAME);
      LOG_DBG("WEB", "[UPLOAD] Rejected unsafe filename: %s", state.fileName.c_str());
      return;
    }

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = normalizeWebPath(server->arg("path"));
    } else {
      state.path = "/";
    }

    if (state.path.isEmpty()) {
      state.error = trWeb(lang, StrId::STR_WEB_PROTECTED_PATH);
      return;
    }

    LOG_INF("WEB", "Upload begin name=%s heap=%u largest=%u stack=%u", state.fileName.c_str(), ESP.getFreeHeap(),
            ESP.getMaxAllocHeap(), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = String(trWeb(lang, StrId::STR_WEB_FILE_EXISTS)) + " " + state.fileName;
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = trWeb(lang, StrId::STR_WEB_CREATE_FILE_FAILED);
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = trWeb(lang, StrId::STR_WEB_WRITE_FAILED_DISK);
            state.file.close();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = trWeb(lang, StrId::STR_WEB_WRITE_FINAL_FAILED);
      }
      state.file.close();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_INF("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache after uploading the file
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCache(filePath.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
    }
    state.error = trWeb(lang, StrId::STR_WEB_UPLOAD_ABORTED);
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  const Language lang = requestLanguage();
  if (state.success) {
    server->send(200, "text/plain", String(trWeb(lang, StrId::STR_WEB_UPLOAD_OK)) + " " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? String(trWeb(lang, StrId::STR_WEB_UPLOAD_UNKNOWN_ERROR))
                                             : state.error;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  const Language lang = requestLanguage();
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_FOLDER_NAME));
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_FOLDER_NAME_EMPTY));
    return;
  }
  if (!FsHelpers::isSafePathComponent(folderName)) {
    LOG_DBG("WEB", "Rejected unsafe folder name: %s", folderName.c_str());
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_FOLDER_NAME));
    return;
  }
  if (isProtectedItemName(folderName)) {
    LOG_DBG("WEB", "Rejected protected folder name: %s", folderName.c_str());
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_CANNOT_CREATE_PROTECTED));
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }

  if (parentPath.isEmpty()) {
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_PROTECTED_PATH));
    return;
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_FOLDER_EXISTS));
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", String(trWeb(lang, StrId::STR_WEB_FOLDER_CREATED)) + " " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_CREATE_FOLDER_FAILED));
  }
}

void CrossPointWebServer::handleRename() const {
  const Language lang = requestLanguage();
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_PATH_OR_NAME));
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_PATH));
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_NEW_NAME_EMPTY));
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_FILE_NAME));
    return;
  }
  if (isProtectedItemName(newName)) {
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_CANNOT_RENAME_TO_PROTECTED));
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_CANNOT_RENAME_PROTECTED));
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", trWeb(lang, StrId::STR_WEB_NAME_UNCHANGED));
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", trWeb(lang, StrId::STR_WEB_ITEM_NOT_FOUND));
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_FAILED_OPEN_FILE));
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_ONLY_FILES_RENAME));
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", trWeb(lang, StrId::STR_WEB_TARGET_EXISTS));
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", trWeb(lang, StrId::STR_WEB_RENAMED));
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_RENAME_FAILED));
  }
}

void CrossPointWebServer::handleMove() const {
  const Language lang = requestLanguage();
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_PATH_OR_DEST));
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_PATH));
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_DEST));
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_CANNOT_MOVE_PROTECTED));
    return;
  }
  if (destPath != "/") {
    const String destName = destPath.substring(destPath.lastIndexOf('/') + 1);
    if (isProtectedItemName(destName)) {
      server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_CANNOT_MOVE_INTO_PROTECTED));
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", trWeb(lang, StrId::STR_WEB_ITEM_NOT_FOUND));
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_FAILED_OPEN_FILE));
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_ONLY_FILES_MOVE));
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", trWeb(lang, StrId::STR_WEB_DEST_NOT_FOUND));
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_DEST_NOT_FOLDER));
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", trWeb(lang, StrId::STR_WEB_ALREADY_IN_DEST));
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", trWeb(lang, StrId::STR_WEB_TARGET_EXISTS));
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", trWeb(lang, StrId::STR_WEB_MOVED));
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", trWeb(lang, StrId::STR_WEB_MOVE_FAILED));
  }
}

void CrossPointWebServer::handleDelete() const {
  const Language lang = requestLanguage();
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_DELETE_ARG));
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_PROVIDE_PATH_OR_PATHS));
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_PATHS_FORMAT));
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_NO_PATHS));
    return;
  }

  for (const auto& p : paths) {
    if (!p.is<const char*>() || normalizeWebPath(p.as<String>()).isEmpty() || p.as<String>() == "/") {
      server->send(403, "text/plain", trWeb(lang, StrId::STR_WEB_PROTECTED_PATH));
      return;
    }
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = normalizeWebPath(p.as<String>());

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (" + String(trWeb(lang, StrId::STR_WEB_DELETE_REASON_ROOT)) + "); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (" + String(trWeb(lang, StrId::STR_WEB_DELETE_REASON_NOT_FOUND)) + "); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (" + String(trWeb(lang, StrId::STR_WEB_DELETE_REASON_FOLDER_NOT_EMPTY)) + "); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) - remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (" + String(trWeb(lang, StrId::STR_WEB_DELETE_REASON_FAILED)) + "); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", trWeb(lang, StrId::STR_WEB_DELETE_ALL_OK));
  } else {
    server->send(500, "text/plain",
                String(trWeb(lang, StrId::STR_WEB_DELETE_SOME_FAILED)) + " " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendStaticContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml), SettingsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  const Language lang = requestLanguage();
  // Pass the SD font registry so the fontFamily setting's enumStringValues
  // includes SD-resident families - otherwise the web API only exposes the
  // three built-in fonts.
  const auto& settings = getSettingsList(&sdFontSystem.registry());

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& s : settings) {
    if (!s.key) continue;  // Skip ACTION-only entries

    doc.clear();
    doc["key"] = s.key;
    doc["name"] = plainButtonText(trWeb(lang, s.nameId));
    doc["category"] = trWeb(lang, s.category);

    switch (s.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        break;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        } else if (s.valueGetter) {
          doc["value"] = static_cast<int>(s.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        if (!s.enumStringValues.empty()) {
          for (const auto& opt : s.enumStringValues) {
            options.add(opt);
          }
        } else {
          for (const auto& opt : s.enumValues) {
            options.add(trWeb(lang, opt));
          }
        }
        break;
      }
      case SettingType::VALUE: {
        doc["type"] = "value";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        doc["min"] = s.valueRange.min;
        doc["max"] = s.valueRange.max;
        doc["step"] = s.valueRange.step;
        break;
      }
      case SettingType::STRING: {
        doc["type"] = "string";
        if (s.stringGetter) {
          doc["value"] = s.stringGetter();
        } else if (s.stringMaxLen > 0) {
          doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset;
        }
        break;
      }
      default:
        continue;
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s", s.key);
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  const Language lang = requestLanguage();
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_JSON));
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain",
                  String(trWeb(lang, StrId::STR_WEB_INVALID_JSON)) + " " + err.c_str());
    return;
  }

  const auto& settings = getSettingsList(&sdFontSystem.registry());
  int applied = 0;

  for (const auto& s : settings) {
    if (!s.key) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumValues.size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          if (s.valuePtr) {
            if (s.valuePtr == &CrossPointSettings::clockUtcOffsetQ && SETTINGS.clockUtcOffsetQ != val) {
              SETTINGS.clockAutoTimezone = 0;
            }
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
    char appliedBody[64];
  snprintf(appliedBody, sizeof(appliedBody), trWeb(lang, StrId::STR_WEB_SETTINGS_APPLIED_FORMAT), applied);
  server->send(200, "text/plain", appliedBody);
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    // Never expose passwords over the API - only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  const Language lang = requestLanguage();
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_JSON));
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain",
                  String(trWeb(lang, StrId::STR_WEB_INVALID_JSON)) + " " + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password - the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_SERVER_INDEX));
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing && existing->url == opdsServer.url && existing->username == opdsServer.username) {
        password = existing->password;
      }
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_SERVER_LIMIT));
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  const Language lang = requestLanguage();
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_JSON));
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain",
                  String(trWeb(lang, StrId::STR_WEB_INVALID_JSON)) + " " + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_INDEX));
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_SERVER_INDEX));
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto credentials = WIFI_STORE.getCredentialSummaries();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API - only indicate whether one is set
    doc["hasPassword"] = credentials[i].hasPassword;
    doc["isLastConnected"] = credentials[i].isLastConnected;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  const Language lang = requestLanguage();
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_JSON));
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain",
                  String(trWeb(lang, StrId::STR_WEB_INVALID_JSON)) + " " + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_SSID_REQUIRED));
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_NETWORK_INDEX));
      return;
    }
    const auto credential = WIFI_STORE.getCredentialAt(static_cast<size_t>(idx));
    if (!credential) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_NETWORK_INDEX));
      return;
    }

    const std::string oldSsid = credential->ssid;
    if (!hasPasswordField) {
      password = credential->password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_WIFI_UPDATE_FAILED));
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_NETWORK_LIMIT));
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  const Language lang = requestLanguage();
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_JSON));
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain",
                  String(trWeb(lang, StrId::STR_WEB_INVALID_JSON)) + " " + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_MISSING_INDEX));
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_NETWORK_INDEX));
    return;
  }
  const auto ssid = WIFI_STORE.getSsidAt(static_cast<size_t>(idx));
  if (!ssid) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_INVALID_NETWORK_INDEX));
    return;
  }

  if (!WIFI_STORE.removeCredential(*ssid)) {
    server->send(400, "text/plain", trWeb(lang, StrId::STR_WEB_WIFI_DELETE_FAILED));
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid->c_str());
  server->send(200, "text/plain", "OK");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED || type == WStype_DISCONNECTED) wsAuthenticated[num] = false;
  if (type != WStype_CONNECTED && type != WStype_DISCONNECTED && !wsAuthenticated[num]) {
    const std::string_view text(reinterpret_cast<const char*>(payload), length);
    if (type == WStype_TEXT && text.substr(0, 5) == "AUTH:" && auth.tokenMatches(text.substr(5))) {
      wsAuthenticated[num] = true;
      wsServer->sendTXT(num, "AUTHENTICATED");
    } else {
      wsServer->disconnect(num);
    }
    return;
  }
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg;
      msg.concat(reinterpret_cast<const char*>(payload), length);

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          if (!FsHelpers::isSafePathComponent(wsUploadFileName) || isProtectedItemName(wsUploadFileName)) {
            LOG_DBG("WS", "START rejected: invalid filename '%s'", wsUploadFileName.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid file name");
            return;
          }
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          if (wsUploadPath.isEmpty()) {
            wsServer->sendTXT(num, "ERROR:Protected path");
            return;
          }
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = wsLastActivityTime = millis();

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          resetTaskWatchdogIfSubscribed();
          if (Storage.exists(filePath.c_str())) {
            LOG_DBG("WS", "Upload collision: %s", filePath.c_str());
            wsServer->sendTXT(num, "ERROR:File already exists: " + wsUploadFileName);
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Open file for writing
          resetTaskWatchdogIfSubscribed();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          resetTaskWatchdogIfSubscribed();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            wsUploadFile.close();
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCache(filePath.c_str());
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      size_t written = wsUploadFile.write(payload, length);
      resetTaskWatchdogIfSubscribed();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;
      wsLastActivityTime = millis();

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache after uploading the file
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCache(filePath.c_str());

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  sendStaticContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml), FontsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  // The synchronous HTTP handler reuses the transfer arena. Coalesce small
  // JSON fragments so TCP does not queue hundreds of tiny chunked writes.
  size_t buffered = 0;
  const auto flush = [&]() -> bool {
    if (buffered == 0) return true;
    server->sendContent(reinterpret_cast<const char*>(upload.buffer.data()), buffered);
    buffered = 0;
    resetTaskWatchdogIfSubscribed();
    return server->client().connected();
  };
  const auto append = [&](const char* text) -> bool {
    size_t remaining = strlen(text);
    while (remaining > 0) {
      const size_t count = std::min(remaining, upload.buffer.size() - buffered);
      memcpy(upload.buffer.data() + buffered, text, count);
      buffered += count;
      text += count;
      remaining -= count;
      if (buffered == upload.buffer.size() && !flush()) return false;
    }
    return true;
  };

  // Keep only one name or file record in RAM, regardless of catalog size.
  JsonDocument doc;
  String json;
  const auto sendRecord = [&]() -> bool {
    json = "";
    const size_t length = measureJson(doc);
    bool allocated = !doc.overflowed();
#ifndef SIMULATOR
    allocated = allocated && json.reserve(length);
#endif
    if (!allocated) {
      LOG_ERR("WEB", "Font catalog response allocation failed");
      server->client().stop();
      return false;
    }
    if (serializeJson(doc, json) != length) {
      server->client().stop();
      return false;
    }
    return append(json.c_str());
  };
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  if (!append("{\"families\":[")) return;
  bool firstFamily = true;
  char value[64];
  for (const auto& family : families) {
    if (!firstFamily && !append(",")) return;
    firstFamily = false;
    if (!append("{\"name\":")) return;
    doc.clear();
    doc.set(family.name.c_str());
    if (!sendRecord()) return;
    if (!append(",\"sizes\":[")) return;
    bool firstSize = true;
    for (const uint8_t size : family.availableSizes()) {
      snprintf(value, sizeof(value), "%s%u", firstSize ? "" : ",", static_cast<unsigned>(size));
      firstSize = false;
      if (!append(value)) return;
    }
    if (!append("],\"files\":[")) return;
    bool firstFile = true;
    for (const auto& file : family.files) {
      if (!firstFile && !append(",")) return;
      firstFile = false;
      doc.clear();
      const char* name = strrchr(file.path.c_str(), '/');
      doc["name"] = name ? name + 1 : file.path.c_str();
      HalFile font;
      const bool opened = Storage.openFileForRead("WEB", file.path.c_str(), font);
      doc["size"] = opened ? static_cast<unsigned long>(font.size()) : 0;
      if (opened) font.close();
      if (!sendRecord()) return;
      resetTaskWatchdogIfSubscribed();
    }
    if (!append("]}")) return;
  }
  snprintf(value, sizeof(value), "],\"maxFamilies\":%d}", SdCardFontRegistry::MAX_SD_FAMILIES);
  if (!append(value)) return;
  if (!flush()) return;
  server->sendContent("");
  LOG_INF("WEB", "Font catalog streamed families=%u heap=%u largest=%u", static_cast<unsigned>(families.size()),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      // Build and validate the complete destination path before touching the
      // SD card: an invalid or over-long path must not create a directory or
      // open a truncated target for writing. A failure leaves fontUpload
      // invalid, so the upload reports the existing 400 without cleanup work.
      char path[FontInstaller::MAX_FONT_PATH_SIZE];
      if (!FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path))) {
        LOG_ERR("WEB", "Invalid font path: %s/%s", family.c_str(), filename.c_str());
        break;
      }

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      if (Storage.exists(path)) {
        LOG_ERR("WEB", "Font file already exists");
        break;
      }
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      const size_t received = fontUpload.bytesWritten + fontUpload.bufferPos;
      constexpr char magic[] = "CPFONT\0\0";
      for (size_t i = 0; i < upload.currentSize && received + i < 8; ++i) {
        if (upload.buf[i] != static_cast<uint8_t>(magic[received + i])) {
          fontUpload.valid = false;
          break;
        }
      }
      if (!fontUpload.valid) break;
      if (received + upload.currentSize >= 8) fontUpload.magicChecked = true;

      // Font and general uploads share the serial HTTP handler's arena.
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = UploadState::UPLOAD_BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(this->upload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (fontUpload.file.write(this->upload.buffer.data(), fontUpload.bufferPos) != fontUpload.bufferPos) {
            fontUpload.valid = false;
            break;
          }
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      fontUpload.valid = fontUpload.valid && fontUpload.magicChecked;
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        if (fontUpload.file.write(this->upload.buffer.data(), fontUpload.bufferPos) != fontUpload.bufferPos) {
          fontUpload.valid = false;
        }
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        if (fontUpload.valid && !fontUpload.file.sync()) fontUpload.valid = false;
        fontUpload.file.close();
      }

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      if (fontUpload.file) {
        fontUpload.file.close();
      }
      if (!fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  const Language lang = requestLanguage();
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", webErrorBody(lang, StrId::STR_WEB_INVALID_CPFONT));
  }
}

void CrossPointWebServer::handleFontDelete() {
  const Language lang = requestLanguage();
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", webErrorBody(lang, StrId::STR_WEB_INVALID_REQUEST));
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", webErrorBody(lang, StrId::STR_WEB_FONT_DELETE_FAILED));
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
