#pragma once
#include "esphome/core/helpers.h"
#include <map>
#include <mutex>
#include <cassert>
#include <cstdlib>
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4;
namespace artwork_heap_fixture {
inline std::mutex mutex;
inline std::map<void *,size_t> allocations;
}
inline void *heap_caps_malloc(size_t bytes,unsigned caps) {
  assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(esphome::host_alloc_fail || esphome::host_alloc_count++>=esphome::host_fail_after)return nullptr;
  void *value=std::malloc(bytes);
  if(value){std::lock_guard<std::mutex> lock(artwork_heap_fixture::mutex);artwork_heap_fixture::allocations[value]=bytes;}
  return value;
}
inline void heap_caps_free(void *pointer) {
  if(!pointer)return;
  {std::lock_guard<std::mutex> lock(artwork_heap_fixture::mutex);
   assert(artwork_heap_fixture::allocations.erase(pointer)==1);}
  std::free(pointer);
}
