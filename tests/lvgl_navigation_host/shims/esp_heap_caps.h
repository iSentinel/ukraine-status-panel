#pragma once
#include <cstdlib>
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline unsigned map_allocation_count = 0;
inline size_t map_allocation_bytes = 0;
inline void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t) {
  void *result = nullptr;
  if (posix_memalign(&result, alignment, bytes) != 0) return nullptr;
  ++map_allocation_count;
  map_allocation_bytes += bytes;
  return result;
}

constexpr uint32_t MALLOC_CAP_INTERNAL = 4;
inline size_t heap_caps_get_free_size(uint32_t) { return 50000; }
