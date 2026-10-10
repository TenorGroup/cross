#pragma once

#include <Utf8.h>

#include <string>
#include <vector>

// Only the subject may be shortened; the consequence always reaches the last line intact.
template <typename Wrap, typename Measure>
std::vector<std::string> confirmationMessageLines(const std::string& message, const std::string& subject,
                                                  int maxLines, Wrap&& wrap, Measure&& measure) {
  auto lines = wrap(message.c_str(), maxLines + 1);
  if (static_cast<int>(lines.size()) <= maxLines || subject.empty() || !message.starts_with(subject)) return lines;

  const auto suffix = message.substr(subject.size());
  const auto dot = subject.find_last_of('.');
  const bool hasExtension = dot != std::string::npos && subject.size() - dot <= 8;
  const auto stem = hasExtension ? subject.substr(0, dot) : subject;
  const auto extension = hasExtension ? subject.substr(dot) : std::string{};
  int low = measure("\xE2\x80\xA6");
  int high = measure(stem.c_str()) - 1;
  while (low <= high) {
    const int width = low + (high - low) / 2;
    const auto candidate = utf8MiddleEllipsis(stem, width, measure) + extension + suffix;
    auto fitted = wrap(candidate.c_str(), maxLines + 1);
    if (static_cast<int>(fitted.size()) <= maxLines) {
      lines = std::move(fitted);
      low = width + 1;
    } else {
      high = width - 1;
    }
  }
  return lines;
}
