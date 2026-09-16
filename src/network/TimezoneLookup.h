#pragma once
namespace timezone_lookup {
// Updates only the offset after a complete, validated response. Caller saves settings.
bool updateOffset();
}  // namespace timezone_lookup
