#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t fnv1_hash(const char *text) {
  uint32_t hash = 2166136261U;
  while (*text) { hash *= 16777619U; hash ^= uint8_t(*text++); }
  return hash;
}
}
