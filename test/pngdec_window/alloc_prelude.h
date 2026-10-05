// Forced into PNGdec.cpp only: its malloc/free go through the test's capped allocator.
#pragma once
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void* pngdec_test_malloc(size_t bytes);
void pngdec_test_free(void* ptr);
#ifdef __cplusplus
}
#endif
#define malloc(bytes) pngdec_test_malloc(bytes)
#define free(ptr) pngdec_test_free(ptr)
