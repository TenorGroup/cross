#pragma once

#include <HalStorage.h>
#include <WebServer.h>

#include <functional>

#include "WebTransferAuth.h"

class WebDAVHandler : public RequestHandler {
 public:
  explicit WebDAVHandler(const WebTransferAuth& access, std::function<bool()> cancel = {},
                         std::function<void()> noteSocket = {}, std::function<void()> retractSocket = {})
      : auth(access), cancel_(std::move(cancel)), noteSocket_(std::move(noteSocket)),
        retractSocket_(std::move(retractSocket)) {}
  // RequestHandler interface
  bool canHandle(WebServer& server, HTTPMethod method, const String& uri) override;
  bool canRaw(WebServer& server, const String& uri) override;
  void raw(WebServer& server, const String& uri, HTTPRaw& raw) override;
  bool handle(WebServer& server, HTTPMethod method, const String& uri) override;

 private:
  const WebTransferAuth& auth;
  // PUT streaming state (raw() is called in chunks)
  HalFile _putFile;
  String _putPath;
  bool _putOk = false;
  bool _putExisted = false;
  std::function<bool()> cancel_;
  std::function<void()> noteSocket_;
  std::function<void()> retractSocket_;

  bool transferCancelled() const { return cancel_ && cancel_(); }

  // WebDAV method handlers
  void handleOptions(WebServer& s);
  void handlePropfind(WebServer& s);
  void handleGet(WebServer& s);
  void handleHead(WebServer& s);
  void handlePut(WebServer& s);
  void handleDelete(WebServer& s);
  void handleMkcol(WebServer& s);
  void handleMove(WebServer& s);
  void handleCopy(WebServer& s);
  void handleLock(WebServer& s);
  void handleUnlock(WebServer& s);

  // Utilities
  String getRequestPath(WebServer& s) const;
  String getDestinationPath(WebServer& s) const;
  void urlEncodePath(const String& path, String& out) const;
  bool isProtectedPath(const String& path) const;
  int getDepth(WebServer& s) const;
  bool getOverwrite(WebServer& s) const;
  void sendPropEntry(WebServer& s, const String& href, bool isDir, size_t size, const String& lastModified) const;
  static String getMimeType(const String& path);
};
