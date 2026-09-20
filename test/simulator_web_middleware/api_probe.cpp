#include <WebServer.h>

void registerMiddleware(WebServer& server) {
  server.addMiddleware([](WebServer&, Middleware::Callback next) { return next(); });
}
