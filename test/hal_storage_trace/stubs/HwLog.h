#pragma once

#include <string>
#include <vector>

// Every pin, bus and delay call the code under test makes, in order.
inline std::vector<std::string> hwLog;
