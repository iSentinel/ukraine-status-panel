#pragma once
#include <memory>
#include <cstdlib>
#include "usp_spotify_pkce.h"
namespace esphome {
inline bool host_alloc_fail = false, host_rng_fail = false;
inline size_t host_alloc_count=0,host_fail_after=SIZE_MAX;
template<typename T> using RAMUniquePtr = std::unique_ptr<T>;
template<typename T> struct ExternalRAMAllocator {
  enum { ALLOC_EXTERNAL = 1 };
  explicit ExternalRAMAllocator(int flags) {if(flags!=ALLOC_EXTERNAL)std::abort();}
  T *allocate(size_t n) { if(host_alloc_fail || host_alloc_count++>=host_fail_after)return nullptr; return static_cast<T *>(std::malloc(n*sizeof(T))); }
  T *reallocate(T *p,size_t n) { return static_cast<T *>(std::realloc(p,n*sizeof(T))); }
  void deallocate(T *p,size_t) { std::free(p); }
  template<typename... Args> std::unique_ptr<T> make_unique(Args&&...args) {
    if(host_alloc_fail || host_alloc_count++>=host_fail_after)return {};
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }
  std::unique_ptr<T[]> make_unique_array_for_overwrite(size_t size) {
    if(host_alloc_fail || host_alloc_count++>=host_fail_after) return {};
    return std::unique_ptr<T[]>(new T[size]);
  }
};
}
namespace usp_spotify_pkce {
/** Deterministic host entropy replaces only the real ESP32 entropy dependency. */
inline bool begin(Session *session,uint32_t now) {
  static uint8_t counter=0;
  if(esphome::host_rng_fail) return false;
  ++counter;
  std::array<uint8_t,VERIFIER_BYTES> entropy{};entropy.fill(counter);
  std::array<uint8_t,STATE_BYTES> state{};state.fill(counter+1);
  std::array<uint8_t,SHA256_BYTES> digest{};digest.fill(counter+2);
  return begin_from_material(session,now,entropy,state,digest);
}
}
