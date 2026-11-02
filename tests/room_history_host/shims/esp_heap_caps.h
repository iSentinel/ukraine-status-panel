#pragma once
#include <cstdlib>
#include <vector>
constexpr unsigned MALLOC_CAP_SPIRAM = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
namespace room_test_heap {
enum class Failure { NONE, PSRAM_ONLY, ALL };
inline Failure failure{Failure::NONE};
inline unsigned calls{0};
inline std::vector<void *> blocks;
inline void release() {
  for (void *block : blocks) std::free(block);
  blocks.clear();
}
inline void reset(Failure value = Failure::NONE) {
  release();
  failure = value;
  calls = 0;
}
}
inline void *heap_caps_calloc(size_t count, size_t size, unsigned flags) {
  ++room_test_heap::calls;
  if (room_test_heap::failure == room_test_heap::Failure::ALL ||
      (room_test_heap::failure == room_test_heap::Failure::PSRAM_ONLY &&
       (flags & MALLOC_CAP_SPIRAM))) return nullptr;
  void *memory = std::calloc(count, size);
  if (memory) room_test_heap::blocks.push_back(memory);
  return memory;
}
