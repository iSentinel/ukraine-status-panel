#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::lvgl {

struct NativeCopyRegion {
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
};

struct NativeCopyPlan {
  bool use_dma{false};
  size_t offset_bytes{0};
  size_t copy_bytes{0};
};

inline NativeCopyPlan native_copy_plan(
    const NativeCopyRegion &area, uint32_t width, uint32_t height,
    uint32_t stride, uint8_t bytes_per_pixel, size_t dma_threshold) {
  NativeCopyPlan plan{};
  if (bytes_per_pixel == 0 || width == 0 || height == 0) return plan;
  if (area.x1 != 0 || area.x2 != static_cast<int32_t>(width) - 1) return plan;
  if (area.y1 < 0 || area.y2 < area.y1 ||
      area.y2 >= static_cast<int32_t>(height)) return plan;
  const size_t row_bytes = static_cast<size_t>(width) * bytes_per_pixel;
  if (stride != row_bytes) return plan;
  const size_t rows = static_cast<size_t>(area.y2 - area.y1 + 1);
  if (rows > SIZE_MAX / row_bytes) return plan;
  plan.offset_bytes = static_cast<size_t>(area.y1) * stride;
  plan.copy_bytes = rows * row_bytes;
  plan.use_dma = plan.copy_bytes >= dma_threshold;
  return plan;
}

}  // namespace esphome::lvgl
