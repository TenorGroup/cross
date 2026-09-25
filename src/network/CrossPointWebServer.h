#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WebTransferAuth.h"

namespace power_timeout {
constexpr uint32_t SESSION_IDLE_TIMEOUT_MS = 5u * 60u * 1000u;

class WebSessionLifecycle {
 public:
  void start(const uint32_t now) {
    running_ = true;
    lastActivityTime_ = now;
  }

  void stop() { running_ = false; }

  void noteHttpRequest(const bool authorized, const bool statusEndpoint, const uint32_t now) {
    if (running_ && authorized && !statusEndpoint) lastActivityTime_ = now;
  }

  void noteMeaningfulActivity(const uint32_t now) {
    if (running_) lastActivityTime_ = now;
  }

  void noteTransferBytes(const size_t bytes, const uint32_t now) {
    if (running_ && bytes > 0) lastActivityTime_ = now;
  }

  // Browser pings only prove that a page remains open. They do not represent
  // an operation that should keep the reader awake.
  void noteWebSocketPing(uint32_t) {}

  bool idleExpired(const uint32_t now) const {
    return running_ && now - lastActivityTime_ >= SESSION_IDLE_TIMEOUT_MS;
  }

  bool running() const { return running_; }
  uint32_t lastActivityTime() const { return lastActivityTime_; }

 private:
  bool running_ = false;
  uint32_t lastActivityTime_ = 0;
};
}  // namespace power_timeout

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    UploadState() { buffer.resize(UPLOAD_BUFFER_SIZE); }
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // The owning activity applies and publishes the UI font tier under its
  // RenderLock. A server without an owner callback rejects this setting.
  void setUiTextSizeApplier(std::function<bool(uint8_t)> applier);

  // Asked on every received chunk of an HTTP upload. True drops that upload: the partial
  // file is removed and the client disconnected, so the request no longer holds the loop.
  void setUploadCancel(std::function<bool()> cancel);

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  unsigned long getLastActivityTime() const { return sessionLifecycle.lastActivityTime(); }
  bool sessionIdleExpired(unsigned long now) const;

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  WebTransferAuth auth;
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;
  power_timeout::WebSessionLifecycle sessionLifecycle;
  std::function<bool(uint8_t)> uiTextSizeApplier;
  std::function<bool()> uploadCancel;
  bool uploadCancelled();

  void noteSessionActivity();
  void noteTransferActivity(size_t bytes);
  void abortHttpUploads();

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // Locale of the current request, resolved from its `lang` query argument.
  // Resolved per request; the global I18N language is never mutated.
  Language requestLanguage() const;

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleTheme() const;
  void handleJszip() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload();
  void handleUpload(UploadState& state);
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();
  bool applyUiTextSizeSetting(uint8_t value);

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList();
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    size_t bufferPos = 0;
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
};
