#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include "UploadSocket.h"
struct CrossPointWebServer { UploadSocket uploadSocket; void interruptUpload(); };
#include "production-interrupt.inc"
int main() {
  signal(SIGPIPE, SIG_IGN);
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
  int capacity = 4096;
  setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
  timeval deadline{0, 800000};
  setsockopt(pair[0], SOL_SOCKET, SO_SNDTIMEO, &deadline, sizeof(deadline));
  const int flags = fcntl(pair[0], F_GETFL);
  fcntl(pair[0], F_SETFL, flags | O_NONBLOCK);
  char bytes[1024] = {};
  while (send(pair[0], bytes, sizeof(bytes), 0) > 0) {}
  fcntl(pair[0], F_SETFL, flags);
  CrossPointWebServer server;
  server.uploadSocket.note(pair[0]);
  std::thread writer([&] { send(pair[0], bytes, sizeof(bytes), 0); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  server.interruptUpload();
  writer.join();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  server.uploadSocket.retract();
  close(pair[0]); close(pair[1]);
  std::printf("%s stalled download socket returns after Back: %lld ms\n", ms < 300 ? "PASS" : "FAIL", static_cast<long long>(ms));
  return ms >= 300;
}
