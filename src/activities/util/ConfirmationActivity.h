#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/ConfirmationLayout.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyMarkSeed.h"

struct ConfirmationContent {
  std::string question, message, shortAction, longAction, touchMessage;
  bool destructive = false;
  uint32_t timeoutMs = 0;  // a question nobody answers in this time is cancelled (0: it waits)
};

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;

  ConfirmationContent content;
  ConfirmationLayout layout{};
  std::vector<std::string> questionLines, messageLines;
  int selectedIndex = 1;
  uint32_t shownAt = 0;
  std::atomic<bool> laidOut{false};
  void complete(bool cancelled);
  void layoutCross();
  ugly::MarkState mark_;
  int uglyChoice = 1;  // tenor/ugly: the circle starts on Cancel, the second answer
  mutable ugly::Box uglyAnswers[2]{};

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, ConfirmationContent content = {});

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
