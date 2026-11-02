#pragma once
#include <cassert>
#include <cstdlib>
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
namespace fixture {
inline bool fail=false, external=true;
inline unsigned allocations=0, frees=0;
inline void *base=nullptr;
inline size_t bytes=0;
}
inline void *heap_caps_malloc(size_t bytes, unsigned caps) {
  assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  ++fixture::allocations;
  if(fixture::fail)return nullptr;
  fixture::base=std::malloc(bytes);fixture::bytes=bytes;return fixture::base;
}
inline void heap_caps_free(void *value) {
  assert(value==fixture::base);++fixture::frees;std::free(value);fixture::base=nullptr;
}
