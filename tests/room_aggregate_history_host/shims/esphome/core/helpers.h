
#pragma once
#include <cstdint>
#include <cstring>
namespace esphome { inline uint32_t fnv1_hash(const char *s){uint32_t h=2166136261U;for(;*s;++s){h^=static_cast<uint8_t>(*s);h*=16777619U;}return h;} }
