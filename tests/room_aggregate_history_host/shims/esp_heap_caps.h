
#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
static constexpr unsigned MALLOC_CAP_SPIRAM=1U;
static constexpr unsigned MALLOC_CAP_8BIT=2U;
inline int host_alloc_fail_after=-1;
inline size_t host_alloc_calls=0;
inline size_t host_live_bytes=0;
inline std::unordered_map<void*,size_t> host_alloc_sizes;
inline void *heap_caps_calloc(size_t n,size_t size,unsigned caps){
  if((caps&MALLOC_CAP_SPIRAM)==0)return nullptr;
  if(host_alloc_fail_after>=0 && static_cast<int>(host_alloc_calls)>=host_alloc_fail_after){++host_alloc_calls;return nullptr;}
  ++host_alloc_calls;void *p=std::calloc(n,size);if(p){host_alloc_sizes[p]=n*size;host_live_bytes+=n*size;}return p;
}
inline void heap_caps_free(void *p){if(!p)return;auto it=host_alloc_sizes.find(p);if(it!=host_alloc_sizes.end()){host_live_bytes-=it->second;host_alloc_sizes.erase(it);}std::free(p);}
