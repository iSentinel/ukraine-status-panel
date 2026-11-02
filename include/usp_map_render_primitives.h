#pragma once

#include <algorithm>
#include <cstdint>

namespace usp_map_render_primitives {

struct Run {
  uint32_t offset;
  uint16_t length;
};

struct RunSpan {
  uint32_t first;
  uint32_t last;
};

constexpr bool resolve_run_span(uint32_t offset, uint16_t length,
                                uint32_t pixels, RunSpan &span) {
  if (length == 0U || offset >= pixels) return false;
  if (static_cast<uint32_t>(length) > pixels - offset) return false;
  span = {offset, offset + static_cast<uint32_t>(length) - 1U};
  return true;
}

template <typename Observer>
inline void paint_label(uint16_t *buffer, const uint32_t *starts,
                        const Run *runs, uint32_t pixels, uint8_t label, uint16_t color,
                        Observer observer) {
  for (uint32_t index = starts[label]; index < starts[label + 1]; ++index) {
    const auto &run = runs[index];
    RunSpan span{};
    if (!resolve_run_span(run.offset, run.length, pixels, span)) continue;
    std::fill_n(buffer + span.first, run.length, color);
    observer(span.first, run.length);
  }
}

inline void paint_label(uint16_t *buffer, const uint32_t *starts,
                        const Run *runs, uint32_t pixels, uint8_t label, uint16_t color) {
  paint_label(buffer, starts, runs, pixels, label, color,
              [](uint32_t, uint16_t) {});
}

struct RunFragment {
  uint32_t first;
  uint32_t count;
};

constexpr bool resolve_run_fragment(uint32_t offset, uint16_t length,
                                    uint32_t progress, uint32_t budget,
                                    uint32_t pixels, RunFragment &fragment) {
  RunSpan span{};
  if (budget == 0U || progress >= static_cast<uint32_t>(length)) return false;
  if (!resolve_run_span(offset, length, pixels, span)) return false;
  const uint32_t remaining = static_cast<uint32_t>(length) - progress;
  fragment = {span.first + progress, std::min(budget, remaining)};
  return true;
}

constexpr uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(
      ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

}  // namespace usp_map_render_primitives
