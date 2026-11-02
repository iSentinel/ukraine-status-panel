#pragma once

#include <algorithm>
#include <cstdint>
#include "usp_dashboard_map_runs.h"

namespace usp_dashboard_map_renderer {

constexpr uint16_t rgb565(uint32_t rgb) {
  return usp_map_render_primitives::rgb565(rgb);
}

struct DirtyArea {
  bool changed{false};
  bool full{false};
  uint16_t x1{0};
  uint16_t y1{0};
  uint16_t x2{0};
  uint16_t y2{0};

  void reset() { *this = {}; }

  void set_full() {
    changed = true;
    full = true;
    x1 = 0;
    y1 = 0;
    x2 = usp_dashboard_map_cache_data::WIDTH - 1;
    y2 = usp_dashboard_map_cache_data::HEIGHT - 1;
  }

  void include(uint32_t offset, uint16_t length) {
    if (full) return;
    constexpr uint32_t width = usp_dashboard_map_cache_data::WIDTH;
    constexpr uint32_t pixels =
        width * usp_dashboard_map_cache_data::HEIGHT;
    usp_map_render_primitives::RunSpan span{};
    if (!usp_map_render_primitives::resolve_run_span(
            offset, length, pixels, span)) return;
    const uint32_t first = span.first;
    const uint32_t last = span.last;
    const uint16_t first_y = static_cast<uint16_t>(first / width);
    const uint16_t last_y = static_cast<uint16_t>(last / width);
    uint16_t run_x1 = static_cast<uint16_t>(first % width);
    uint16_t run_x2 = static_cast<uint16_t>(last % width);
    if (first_y != last_y) {
      run_x1 = 0;
      run_x2 = static_cast<uint16_t>(width - 1U);
    }
    if (!changed) {
      changed = true;
      x1 = run_x1;
      y1 = first_y;
      x2 = run_x2;
      y2 = last_y;
      return;
    }
    x1 = std::min(x1, run_x1);
    y1 = std::min(y1, first_y);
    x2 = std::max(x2, run_x2);
    y2 = std::max(y2, last_y);
  }
};

class Renderer {
 public:
  explicit Renderer(uint16_t *buffer) : buffer_(buffer) {}

  bool update(const uint16_t (&colors)[27], DirtyArea *dirty = nullptr) {
    if (dirty != nullptr) dirty->reset();
    bool changed = false;
    if (!ready_ || colors_[0] != colors[0]) {
      std::fill_n(
          buffer_,
          usp_dashboard_map_cache_data::WIDTH *
              usp_dashboard_map_cache_data::HEIGHT,
          colors[0]);
      ready_ = false;
      changed = true;
      if (dirty != nullptr) dirty->set_full();
    }
    for (uint8_t label = 1; label <= 26; ++label) {
      if (!ready_ || colors_[label] != colors[label]) {
        this->paint_(label, colors[label], dirty);
        changed = true;
      }
    }
    std::copy_n(colors, 27, colors_);
    ready_ = true;
    return changed;
  }

 private:
  void paint_(uint8_t label, uint16_t color, DirtyArea *dirty) {
    usp_map_render_primitives::paint_label(
        buffer_, usp_dashboard_map_cache_data::STARTS,
        usp_dashboard_map_cache_data::RUNS,
        usp_dashboard_map_cache_data::WIDTH * usp_dashboard_map_cache_data::HEIGHT,
        label, color,
        [dirty](uint32_t offset, uint16_t length) {
          if (dirty != nullptr) dirty->include(offset, length);
        });
  }

  uint16_t *buffer_;
  uint16_t colors_[27]{};
  bool ready_{};
};

}  // namespace usp_dashboard_map_renderer
