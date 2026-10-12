#pragma once

#include <cstddef>
#include <string>

namespace telex {
struct State {
  size_t standaloneWEnd = 0;
};
// Rewrites at most 32 letters before the cursor. The caller inserts keys for
// which this returns false. maxLength counts UTF-8 bytes, as the entry does.
bool insert(State& state, std::string& text, size_t& cursor, char key, size_t maxLength = 0);
}  // namespace telex
