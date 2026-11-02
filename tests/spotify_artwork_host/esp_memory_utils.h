#pragma once
#include "esp_heap_caps.h"
#include <cstdint>
inline bool esp_ptr_external_ram(const void *pointer) {
  const auto address=reinterpret_cast<uintptr_t>(pointer);
  const auto base=reinterpret_cast<uintptr_t>(fixture::base);
  return fixture::external && address>=base && address-base<fixture::bytes;
}
