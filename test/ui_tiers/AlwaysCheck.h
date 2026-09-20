#pragma once
#include <cstdio>
#include <cstdlib>

// Integration builds define NDEBUG globally. Keep test expressions, including
// fixture setup calls, active in every build configuration.
#define UI_CHECK(condition) \
  do { \
    if (!(condition)) { \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      std::abort(); \
    } \
  } while (false)
