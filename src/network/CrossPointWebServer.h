#pragma once

#include <ArduinoJson.h>
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

#include "FontInstaller.h"
#include "UploadSocket.h"
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
    // One arena for the whole web session: HTTP upload, download, plugin files, quotes export and font upload all
    // reuse it (handlers run serially), so none of them allocates a 4KB block of its own. begin() takes it through
    // the heap floor and stop() gives it back; a server that cannot get it does not start.
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferPos = 0;
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // The owning activity applies and publishes the UI font tier under its
  // RenderLock. A server without an owner callback rejects this setting.
  void setUiTextSizeApplier(std::function<bool(uint8_t)> applier);
  void setFontPackApplier(std::function<bool(const char*)> applier);

  // Asked on every received chunk of an HTTP upload. True drops that upload: the partial
  // file is removed and the client disconnected, so the request no longer holds the loop.
  void setUploadCancel(std::function<bool()> cancel);

  // Safe from another task. A sender that stops mid-upload leaves the library waiting 5 s
  // for the next byte, with no chunk to ask the cancel above; this ends that wait at once,
  // and the upload aborts and removes its partial file.
  void interruptUpload();

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
  std::function<bool(const char*)> fontPackApplier;
  bool prepareFontPackUpload(String& path, const String& filename);
  bool applyFontPackUpload(const String& path, const String& filename);
  std::function<bool()> uploadCancel;
  bool uploadCancelled();
  // Socket of the HTTP upload being read, for interruptUpload().
  UploadSocket uploadSocket;
  void noteUploadSocket();

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
  void handleWebI18n() const;
  void handleJszip() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload();
  void handleQuotesExport();
  // Streams an already-open file to the client through the upload arena, feeding the
  // watchdog per write and aborting cleanly (rather than looping past a dead
  // connection) if a write stalls. Caller sets headers/content-length and
  // closes the file.
  void streamFileToClient(HalFile& file);
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
    bool pack = false;
    bool vector = false;       // .ttf/.otf: header and size are judged by `check`
    const char* code = nullptr;  // why a vector or name check failed (page maps it to text)
    FontInstaller::VectorUploadCheck check;
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

  // Missing or malformed JSON sends a 400 response and returns false.
  bool readJsonBody(JsonDocument& out) const;
  void sendJson(const JsonDocument& doc) const;
  bool rejectIfPluginsDisabled() const;
  void handlePluginList() const;  // GET  /api/plugins   -> discovered plugins
  void handlePluginFile();        // GET  /plugin?name&file -> serve SD file
  void handleRelay();             // POST /api/relay     -> device makes an HTTP(S) call
  void handleCrypto();            // POST /api/crypto    -> generic crypto primitive (base64 I/O)
  void handleFetch();             // POST /api/fetch     -> device downloads a URL to SD
  void handleBookKey();           // POST /api/book-key  -> store a protected book's wrapped content key
  void handlePluginFs();          // POST /api/plugin-fs -> plugin writes a small file to SD
  void handlePluginFsUpload();    // its multipart file part, streamed to <path>.tmp

  // One /api/plugin-fs write in flight: chunks land in `tmp`, which replaces
  // `path` only after a complete, non-empty body.
  struct PluginFsUploadState {
    HalFile file;
    std::string path, tmp;
    size_t bytes = 0;
    bool started = false;
    int errorStatus = 0;  // non-zero: HTTP status to answer with
    const char* error = nullptr;
  } pluginFsUpload;

  // SD-plugin job queue. External systems (a companion app, a script) enqueue
  // {plugin, action, args}; any open page hosting the plugin (File Manager,
  // Settings, or the headless /plugins-run page) claims and executes it, then
  // posts the result. The firmware only stores small JSON blobs — plugin logic
  // never runs on-device. Fixed pool inside this (heap-allocated, web-session
  // lifetime) object: no allocation per job, oldest finished slot recycled.
  struct PluginJob {
    uint32_t id = 0;         // 0 = empty slot
    uint32_t claim = 0;      // current claim; a completion must echo it
    uint32_t updatedAt = 0;  // millis() of last state change
    uint8_t state = 0;
    char plugin[24] = {0};
    char action[24] = {0};
    char args[192] = {0};    // Serialized JSON value
    char result[192] = {0};  // Serialized JSON value from the executor
  };
  static constexpr uint8_t JOB_EMPTY = 0;
  static constexpr uint8_t JOB_PENDING = 1;
  static constexpr uint8_t JOB_RUNNING = 2;
  static constexpr uint8_t JOB_DONE = 3;
  static constexpr uint8_t JOB_ERROR = 4;
  static constexpr size_t MAX_PLUGIN_JOBS = 6;
  static constexpr uint32_t PLUGIN_JOB_LEASE_MS = 10UL * 60 * 1000;
  PluginJob pluginJobs[MAX_PLUGIN_JOBS];
  uint32_t nextPluginJobId = 1;
  uint32_t nextPluginJobClaim = 1;
  PluginJob* allocPluginJob();
  void handlePluginRunnerPage() const;  // GET /plugins-run -> headless executor page
  void handlePluginJobSubmit();         // POST /api/plugin-jobs          -> {id}
  void handlePluginJobClaim();          // GET  /api/plugin-jobs/claim    -> next pending job for a plugin
  void handlePluginJobComplete();       // POST /api/plugin-jobs/complete -> executor posts the outcome
  void handlePluginJobStatus();         // GET  /api/plugin-jobs/status   -> external caller polls

  // An outbound transfer blocks the serving task for its whole duration, so
  // the WebSocket server and discovery UDP cannot answer anyone until it
  // finishes; their buffers are worth more as TLS headroom (4.4KB heap floor
  // measured with them resident during a large transfer).
  void suspendTransferServices();
  void resumeTransferServices();
};
