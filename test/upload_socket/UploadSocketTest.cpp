#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

#include "UploadSocket.h"

namespace {

std::string webServerSource() {
  std::ifstream in(std::string(REPO_ROOT_PATH) + "/src/network/CrossPointWebServer.cpp");
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

TEST(UploadSocket, ShutsTheSocketOfTheUploadBeingRead) {
  UploadSocket socket;
  socket.note(5);
  int shut = -1;
  EXPECT_TRUE(socket.interrupt([&](const int fd) { shut = fd; }));
  EXPECT_EQ(shut, 5);
}

TEST(UploadSocket, ShutsNothingBetweenUploads) {
  UploadSocket socket;
  int shuts = 0;
  EXPECT_FALSE(socket.interrupt([&](int) { ++shuts; }));
  socket.note(5);
  socket.retract();
  EXPECT_FALSE(socket.interrupt([&](int) { ++shuts; }));
  EXPECT_EQ(shuts, 0);
}

// The sampler task reads the socket number and is preempted before it shuts it; meanwhile the
// main task ends the request: it retracts the socket, the server closes it, and the next
// connection is handed the same number. The shut must land on the upload, never on that next
// connection.
TEST(UploadSocket, NeverShutsANumberHandedOnAfterTheUploadEnded) {
  constexpr int FD = 5;
  constexpr int UPLOAD = 1, NEXT = 2;
  std::atomic<int> owner[8] = {};
  owner[FD] = UPLOAD;
  UploadSocket socket;
  socket.note(FD);

  std::atomic<bool> shutStarted{false};
  std::atomic<int> shutHit{0};
  std::thread sampler([&] {
    socket.interrupt([&](const int fd) {
      shutStarted = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      shutHit = owner[fd].load();
    });
  });
  while (!shutStarted) std::this_thread::yield();
  socket.retract();
  owner[FD] = 0;     // the server closes the request's socket
  owner[FD] = NEXT;  // and accepts the next connection on the same number
  sampler.join();
  EXPECT_EQ(shutHit.load(), UPLOAD) << "Back shut a connection that was not the upload";
}

// The web server retracts the socket before the library closes it: in each upload's final
// handler (the body is read by then, the response not yet sent), on an aborted upload, and
// before the owner's cancel stops the client. After handleClient() it is too late.
TEST(UploadSocket, WebServerRetractsBeforeTheSocketCloses) {
  const std::string source = webServerSource();
  ASSERT_FALSE(source.empty());
  const std::regex finals[] = {
      std::regex(R"(server->on\(\s*"/upload", HTTP_POST,\s*\[this\] \{\s*uploadSocket\.retract\(\);)"),
      std::regex(R"(server->on\(\s*"/api/fonts/upload", HTTP_POST,\s*\[this\] \{\s*uploadSocket\.retract\(\);)"),
      std::regex(R"(upload\.status == UPLOAD_FILE_ABORTED\) \{\s*uploadSocket\.retract\(\);)"),
      std::regex(R"(case UPLOAD_FILE_ABORTED: \{\s*uploadSocket\.retract\(\);)"),
      std::regex(R"(uploadSocket\.retract\(\);\s*server->client\(\)\.stop\(\);)"),
  };
  for (const auto& expected : finals) EXPECT_TRUE(std::regex_search(source, expected));
}
