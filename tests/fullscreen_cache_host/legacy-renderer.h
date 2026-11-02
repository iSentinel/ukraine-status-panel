#pragma once

#include <algorithm>
#include <cstdint>
#include "usp_fullscreen_map_runs.h"

namespace usp_map_cache_legacy {

// This renderer has no LVGL or hardware dependency; host tests use this code.
constexpr uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

class Renderer {
 public:
  // The caller owns a WIDTH * HEIGHT RGB565 buffer for the renderer lifetime.
  explicit Renderer(uint16_t *buffer) : buffer_(buffer) {}

  bool update(const uint16_t (&colors)[27]) {
    bool changed = false;
    if (!ready_ || colors_[0] != colors[0]) {
      std::fill_n(buffer_, usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT, colors[0]);
      ready_ = false;
      changed = true;
    }
    for (uint8_t label = 1; label <= 26; ++label) {
      if (!ready_ || colors_[label] != colors[label]) {
        paint_(label, colors[label]);
        changed = true;
      }
    }
    std::copy_n(colors, 27, colors_);
    ready_ = true;
    return changed;
  }

 private:
  void paint_(uint8_t label, uint16_t color) {
    for (uint32_t index = usp_map_cache_data::STARTS[label];
         index < usp_map_cache_data::STARTS[label + 1]; ++index) {
      const auto &run = usp_map_cache_data::RUNS[index];
      std::fill_n(buffer_ + run.offset, run.length, color);
    }
  }

  uint16_t *buffer_;
  uint16_t colors_[27]{};
  bool ready_{};
};

}  // namespace usp_map_cache
