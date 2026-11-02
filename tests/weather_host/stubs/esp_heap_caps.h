#pragma once

#include <cstdlib>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT 0x02

inline void *heap_caps_malloc(size_t size, unsigned) { return std::malloc(size); }
inline void *heap_caps_calloc(size_t count, size_t size, unsigned) {
  return std::calloc(count, size);
}
inline void *heap_caps_realloc(void *ptr, size_t size, unsigned) {
  return std::realloc(ptr, size);
}
inline void heap_caps_free(void *ptr) { std::free(ptr); }
