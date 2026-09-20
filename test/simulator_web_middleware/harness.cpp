#include <WebServer.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
int request(int port, const char* path, bool authorized) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    ::close(fd);
    return 0;
  }
  std::string wire = "GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (authorized) wire += "Authorization: Bearer fixture\r\n";
  wire += "Connection: close\r\n\r\n";
  if (::send(fd, wire.data(), wire.size(), 0) != static_cast<ssize_t>(wire.size())) {
    ::close(fd);
    return 0;
  }
  std::string response;
  char buffer[1024];
  ssize_t count = 0;
  while ((count = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) {
    response.append(buffer, static_cast<size_t>(count));
  }
  ::close(fd);
  const size_t space = response.find(' ');
  return space == std::string::npos ? 0 : std::atoi(response.c_str() + space + 1);
}
}  // namespace

int main(int argc, char** argv) {
  int failures = 0;
  int checks = 0;
  auto check = [&](bool condition, const char* name) {
    ++checks;
    if (!condition) ++failures;
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
  };

  const int port = argc == 2 ? std::atoi(argv[1]) : 0;
  check(port > 0, "loopback port provided");
  if (port <= 0) return 1;

  WebServer server(port);
  std::atomic<int> middlewareCalls{0};
  std::atomic<int> activityTraffic{0};
  std::atomic<int> statusTraffic{0};
  std::mutex traceMutex;
  std::vector<std::string> trace;
  auto record = [&](const std::string& event) {
    std::lock_guard<std::mutex> lock(traceMutex);
    trace.push_back(event);
  };
  auto authorized = [](WebServer& request) {
    return request.header("Authorization") == "Bearer fixture";
  };

#ifndef BASELINE
  server.addMiddleware([&](WebServer& request, Middleware::Callback next) {
    const std::string uri = request.uri().c_str();
    record("before:" + uri);
    const bool handled = next();
    ++middlewareCalls;
    if (authorized(request)) {
      if (uri == "/api/status") {
        ++statusTraffic;
      } else {
        ++activityTraffic;
      }
    }
    record("after:" + uri);
    return handled;
  });
#endif

  server.on("/files", HTTP_GET, [&] {
    record("route:/files");
    server.send(authorized(server) ? 200 : 401, "text/plain", "files");
  });
  server.on("/api/status", HTTP_GET, [&] {
    record("route:/api/status");
    server.send(authorized(server) ? 200 : 401, "application/json", "{}");
  });
  server.onNotFound([&] {
    record("not-found");
    server.send(404, "text/plain", "missing");
  });
  server.begin();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  check(request(port, "/files", true) == 200, "authorized route status");
  check(request(port, "/api/status?poll=1", true) == 200, "authorized status poll");
  check(request(port, "/files", false) == 401, "unauthorized route status");
  check(request(port, "/missing", true) == 404, "authorized not-found status");
  server.stop();

  check(middlewareCalls == 4, "middleware called for every completed request");
  check(activityTraffic == 2, "authorized activity traffic classified");
  check(statusTraffic == 1, "authorized status traffic classified separately");
  const std::vector<std::string> expected = {
      "before:/files",       "route:/files",      "after:/files",
      "before:/api/status",  "route:/api/status", "after:/api/status",
      "before:/files",       "route:/files",      "after:/files",
      "before:/missing",     "not-found",         "after:/missing",
  };
  check(trace == expected, "middleware wraps route and not-found dispatch in order");

  std::cout << "RESULT " << checks - failures << '/' << checks << '\n';
  return failures == 0 ? 0 : 1;
}
