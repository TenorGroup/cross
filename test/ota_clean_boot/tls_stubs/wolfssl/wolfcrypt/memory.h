#pragma once
#include <cstddef>
using wolfSSL_Malloc_cb = void* (*)(size_t);
using wolfSSL_Free_cb = void (*)(void*);
using wolfSSL_Realloc_cb = void* (*)(void*, size_t);
int wolfSSL_GetAllocators(wolfSSL_Malloc_cb*, wolfSSL_Free_cb*, wolfSSL_Realloc_cb*);
int wolfSSL_SetAllocators(wolfSSL_Malloc_cb, wolfSSL_Free_cb, wolfSSL_Realloc_cb);
