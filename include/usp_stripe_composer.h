#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace usp_stripe_composer {

enum class Direction { LEFT, RIGHT };

struct Frame {
  const uint16_t *pixels;
  size_t count;
};

inline bool overlaps(const void *first, size_t first_bytes,
                     const void *second, size_t second_bytes) {
  const auto a = reinterpret_cast<uintptr_t>(first);
  const auto b = reinterpret_cast<uintptr_t>(second);
  const auto limit = std::numeric_limits<uintptr_t>::max();
  if (first_bytes > limit - a || second_bytes > limit - b) return true;
  return a < b + second_bytes && b < a + first_bytes;
}

// Fill only the requested rows. The destination must not share either source.
// The caller owns the sources and must keep them unchanged until scan-out ends.
inline bool compose(Frame old_frame, Frame new_frame, uint16_t *destination,
                    size_t capacity, size_t width, size_t height, size_t first_row,
                    size_t rows, size_t shift, Direction direction) {
  if (width == 0 || width > 1024 || height == 0 || height > 600 ||
      first_row >= height || rows == 0 || rows > height - first_row ||
      shift > width || old_frame.pixels == nullptr || new_frame.pixels == nullptr ||
      destination == nullptr ||
      (direction != Direction::LEFT && direction != Direction::RIGHT))
    return false;
  const size_t frame_size = width * height, stripe_size = width * rows;
  if (old_frame.count < frame_size || new_frame.count < frame_size || capacity < stripe_size)
    return false;
  if (overlaps(destination, stripe_size * 2, old_frame.pixels, frame_size * 2) ||
      overlaps(destination, stripe_size * 2, new_frame.pixels, frame_size * 2))
    return false;
  const size_t left_count = direction == Direction::LEFT ? width - shift : shift;
  for (size_t row = 0; row < rows; ++row) {
    const auto *old_row = old_frame.pixels + (first_row + row) * width;
    const auto *new_row = new_frame.pixels + (first_row + row) * width;
    const auto *left = direction == Direction::LEFT ? old_row + shift : new_row + width - shift;
    const auto *right = direction == Direction::LEFT ? new_row : old_row;
    auto *target = destination + row * width;
    if (left_count) std::memcpy(target, left, left_count * sizeof(uint16_t));
    if (left_count < width)
      std::memcpy(target + left_count, right, (width - left_count) * sizeof(uint16_t));
  }
  return true;
}

}  // namespace usp_stripe_composer
