#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
void *heap_caps_aligned_alloc(size_t alignment,size_t bytes,unsigned flags);
