int main() {
  int failures = 0, cases = 0;
  for (bool twelve : {false, true}) {
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (bool large : {false, true}) {
      SETTINGS.large = large;
      GfxRenderer renderer;
      const auto& font = large ? renderer.bodyFontObject() : renderer.fontObject();
      int timeWidth = 0, batteryText = 0, height = 0;
      char longestClock[12] = "";
      for (int hour = twelve ? 1 : 0; hour <= (twelve ? 12 : 23); ++hour)
        for (int minute = 0; minute < 60; ++minute)
          for (const char* period : {"AM", "PM"}) {
            char clock[12];
            if (twelve) std::snprintf(clock, sizeof(clock), "%d:%02d %s", hour, minute, period);
            else std::snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
            int span = 0;
            font.getTextDimensions(clock, &span, &height);
            if (span > timeWidth) {
              timeWidth = span;
              std::strcpy(longestClock, clock);
            }
          }
      font.getTextDimensions("100", &batteryText, &height);
      const int batteryWidth = (large ? 32 : 26) + 4 + batteryText;
      for (bool swap : {false, true}) {
        for (int screenWidth : {480, 528}) {
          renderer.screenWidth = screenWidth;
          tenorchrome::measuredCorners = {8 + (swap ? timeWidth : batteryWidth),
              screenWidth - 8 - (swap ? batteryWidth : timeWidth)};
          buttonSymbols::paintedBounds.clear();
          TenorTheme{}.drawButtonHints(renderer, "b", "c", "u", "d");
          int previous = tenorchrome::measuredCorners.leftEnd - 1;
          int minimumGap = 1000;
          for (const auto& bounds : buttonSymbols::paintedBounds) {
            minimumGap = std::min(minimumGap, bounds[0] - previous - 1);
            previous = bounds[1];
          }
          minimumGap = std::min(minimumGap, tenorchrome::measuredCorners.rightStart - previous - 1);
          failures += minimumGap < 6;
          ++cases;
          std::printf("12h=%d tier=%d large=%d left=%d width=%d clock=%s clock_width=%d gap=%d %s\n",
                      twelve, tier, large, swap, screenWidth, longestClock, timeWidth, minimumGap,
                      minimumGap < 6 ? "RED" : "GREEN");
        }
      }
    }
  }
  }
  std::printf("%s: %d footer spacing cases\n", failures ? "RED" : "GREEN", cases);
  return failures ? 1 : 0;
}
