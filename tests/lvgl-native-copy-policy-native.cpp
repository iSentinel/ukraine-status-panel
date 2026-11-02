#include "lvgl_native_copy_policy.h"

#include <cassert>
#include <cstdio>

using esphome::lvgl::NativeCopyRegion;
using esphome::lvgl::native_copy_plan;

int main() {
  constexpr uint32_t width = 1024;
  constexpr uint32_t height = 600;
  constexpr uint32_t stride = 2048;
  constexpr size_t threshold = 64U * 1024U;

  const auto full = native_copy_plan(
      {0, 0, 1023, 599}, width, height, stride, 2, threshold);
  assert(full.use_dma);
  assert(full.offset_bytes == 0U);
  assert(full.copy_bytes == 1228800U);

  const auto band = native_copy_plan(
      {0, 100, 1023, 199}, width, height, stride, 2, threshold);
  assert(band.use_dma);
  assert(band.offset_bytes == 204800U);
  assert(band.copy_bytes == 204800U);

  const auto small = native_copy_plan(
      {0, 10, 1023, 19}, width, height, stride, 2, threshold);
  assert(!small.use_dma);
  assert(small.copy_bytes == 20480U);

  assert(!native_copy_plan(
      {10, 0, 1023, 599}, width, height, stride, 2, threshold).use_dma);
  assert(!native_copy_plan(
      {0, 0, 1023, 599}, width, height, 2112, 2, threshold).use_dma);
  assert(!native_copy_plan(
      {0, -1, 1023, 10}, width, height, stride, 2, threshold).use_dma);
  assert(!native_copy_plan(
      {0, 0, 1023, 600}, width, height, stride, 2, threshold).use_dma);

  std::puts(
      "LVGL_NATIVE_COPY_POLICY=PASS full1228800 band204800 "
      "small20480 fallback_partial_stride_bounds");
  return 0;
}
