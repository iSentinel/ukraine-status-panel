#pragma once
#include "esp_heap_caps.h"
#include <cstdint>
inline bool esp_ptr_external_ram(const void *pointer) {
  const auto address=reinterpret_cast<uintptr_t>(pointer);
  std::lock_guard<std::mutex> lock(artwork_heap_fixture::mutex);
  for(const auto &[base,bytes]:artwork_heap_fixture::allocations) {
    const auto first=reinterpret_cast<uintptr_t>(base);
    if(address>=first && address-first<bytes)return true;
  }
  return false;
}

inline bool host_flash_internal_stack=true;
inline bool esp_ptr_internal(const void *pointer) {
  return host_flash_internal_stack && !esp_ptr_external_ram(pointer);
}
