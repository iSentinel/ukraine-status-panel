#pragma once
#include "usp_stripe_composer.h"
#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace usp_stripe_bandwidth {

struct Result {
  uint32_t rows_done{}, total_us{}, stripe_max_us{}, stripe_sum_us{}, stripes{};
  uint32_t deadline_us{}, over_deadline{}, scratch_rows{};
  size_t heap_before{}, heap_after{}, scratch_bytes{};
  bool accepted{}, complete{};
};

// Copy into a small internal buffer. Neither source nor the LCD is written.
inline Result measure(const uint16_t *old_frame, const uint16_t *new_frame,
                      uint16_t *scratch, uint32_t shift, bool reverse,
                      uint32_t rows, uint32_t line_ns) {
  Result result;
  result.accepted = true;
  result.scratch_rows = rows;
  result.scratch_bytes = 1024U * rows * sizeof(uint16_t);
  result.deadline_us = rows * line_ns / 1000U;
  const uint32_t started = esphome::micros();
  for (uint32_t row = 0; row < 600; row += rows) {
    const uint32_t before = esphome::micros();
    const bool ok = usp_stripe_composer::compose(
        {old_frame, 1024U * 600}, {new_frame, 1024U * 600}, scratch, 1024U * rows,
        1024, 600, row, rows, shift,
        reverse ? usp_stripe_composer::Direction::RIGHT : usp_stripe_composer::Direction::LEFT);
    const uint32_t elapsed = esphome::micros() - before;
    if (!ok) break;
    ++result.stripes;
    result.rows_done += rows;
    result.stripe_sum_us += elapsed;
    result.stripe_max_us = std::max(result.stripe_max_us, elapsed);
    result.over_deadline += elapsed > result.deadline_us;
    if (esphome::micros() - started >= 100000U) break;
  }
  result.total_us = esphome::micros() - started;
  result.complete = result.rows_done == 600;
  return result;
}

inline void report(const Result &r, uint32_t shift, bool reverse) {
  ESP_LOGI("usp.stripe",
    "STRIPE_BANDWIDTH shift=%lu reverse=%u accepted=%u complete=%u rows=%lu "
    "scratch_rows=%lu scratch_bytes=%u total_us=%lu stripe_max_us=%lu stripe_avg_us=%lu "
    "deadline_us=%lu over_deadline=%lu heap_before=%u heap_after=%u",
    static_cast<unsigned long>(shift), static_cast<unsigned>(reverse),
    static_cast<unsigned>(r.accepted), static_cast<unsigned>(r.complete),
    static_cast<unsigned long>(r.rows_done), static_cast<unsigned long>(r.scratch_rows),
    static_cast<unsigned>(r.scratch_bytes), static_cast<unsigned long>(r.total_us),
    static_cast<unsigned long>(r.stripe_max_us),
    static_cast<unsigned long>(r.stripes ? r.stripe_sum_us / r.stripes : 0),
    static_cast<unsigned long>(r.deadline_us), static_cast<unsigned long>(r.over_deadline),
    static_cast<unsigned>(r.heap_before), static_cast<unsigned>(r.heap_after));
}

inline Result run(const uint16_t *old_frame, const uint16_t *new_frame,
                  uint32_t shift, bool reverse, uint32_t line_ns) {
  Result result;
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t available = heap_caps_get_free_size(caps);
  const size_t largest = heap_caps_get_largest_free_block(caps);
  const uint32_t rows = available >= 48U * 1024U && largest >= 8192U ? 4U : 2U;
  const size_t bytes = 1024U * rows * sizeof(uint16_t);
  result.heap_before = result.heap_after = available;
  if (old_frame == nullptr || new_frame == nullptr || old_frame == new_frame ||
      shift > 1024 || line_ns == 0 || available < 40U * 1024U + bytes || largest < bytes) {
    report(result, shift, reverse);
    return result;
  }
  auto *scratch = static_cast<uint16_t *>(heap_caps_malloc(bytes, caps));
  if (scratch == nullptr) {
    report(result, shift, reverse);
    return result;
  }
  result = measure(old_frame, new_frame, scratch, shift, reverse, rows, line_ns);
  heap_caps_free(scratch);
  result.heap_before = available;
  result.heap_after = heap_caps_get_free_size(caps);
  report(result, shift, reverse);
  return result;
}

}  // namespace usp_stripe_bandwidth
