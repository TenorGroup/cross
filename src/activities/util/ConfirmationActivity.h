#pragma once
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;

  OptionPopup confirmPopup;
  ugly::MarkState mark_;
  int uglyChoice = 1;  // tenor/ugly: the circle starts on Cancel, the second answer
  mutable ugly::Box uglyAnswers[2]{};

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
