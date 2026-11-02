#pragma once

#include <algorithm>
#include <cstdint>
#include "usp_fullscreen_map_runs.h"

namespace usp_map_cache {

// This renderer has no LVGL or hardware dependency; host tests use this code.
constexpr uint16_t rgb565(uint32_t rgb) {
  return usp_map_render_primitives::rgb565(rgb);
}

class Renderer {
 public:
  // The caller owns a WIDTH * HEIGHT RGB565 buffer for the renderer lifetime.
  explicit Renderer(uint16_t *buffer) : buffer_(buffer) {}

  bool ready() const { return ready_; }
  bool cold_pending() const { return cold_pending_; }
  uint32_t cold_step_pixels() const { return cold_step_pixels_; }

  /** Capture one unfinished frame; a superseding request restarts it safely. */
  bool cold_begin(const uint16_t (&colors)[27]) {
    if (ready_) return false;
    if (cold_pending_ && std::equal(colors, colors + 27, cold_colors_)) return true;
    std::copy_n(colors, 27, cold_colors_);
    cold_background_ = 0;
    cold_label_ = 1;
    cold_run_ = usp_map_cache_data::STARTS[1];
    cold_run_offset_ = 0;
    cold_step_pixels_ = 0;
    cold_pending_ = true;
    return true;
  }

  /** Write at most the requested pixel count without publishing a partial frame. */
  bool cold_step(uint32_t budget) {
    cold_step_pixels_ = 0;
    if (!cold_pending_ || budget == 0) return ready_;
    const uint32_t initial = budget;
    const uint32_t pixels = usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT;
    const uint32_t count = std::min(budget, pixels - cold_background_);
    std::fill_n(buffer_ + cold_background_, count, cold_colors_[0]);
    cold_background_ += count;
    budget -= count;
    if (cold_background_ == pixels) this->paint_cold_step_(budget);
    cold_step_pixels_ = initial - budget;
    if (cold_label_ <= 26) return false;
    std::copy_n(cold_colors_, 27, colors_);
    ready_ = true;
    cold_pending_ = false;
    return true;
  }

  bool update(const uint16_t (&colors)[27]) {
    cold_pending_ = false;
    cold_step_pixels_ = 0;
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
  /** Walk bounded mask runs and keep the next unfinished pixel position. */
  void paint_cold_step_(uint32_t &budget) {
    while (cold_label_ <= 26 && budget != 0) {
      if (cold_run_ >= usp_map_cache_data::STARTS[cold_label_ + 1]) {
        ++cold_label_;
        if (cold_label_ <= 26) cold_run_ = usp_map_cache_data::STARTS[cold_label_];
        cold_run_offset_ = 0;
        continue;
      }
      const auto &run = usp_map_cache_data::RUNS[cold_run_];
      usp_map_render_primitives::RunFragment fragment{};
      constexpr uint32_t pixels =
          usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT;
      if (!usp_map_render_primitives::resolve_run_fragment(
              run.offset, run.length, cold_run_offset_, budget, pixels, fragment)) {
        ++cold_run_;
        cold_run_offset_ = 0;
        continue;
      }
      std::fill_n(buffer_ + fragment.first, fragment.count, cold_colors_[cold_label_]);
      budget -= fragment.count;
      cold_run_offset_ += fragment.count;
      if (cold_run_offset_ == run.length) {
        ++cold_run_;
        cold_run_offset_ = 0;
      }
    }
  }

  void paint_(uint8_t label, uint16_t color) {
    usp_map_render_primitives::paint_label(
        buffer_, usp_map_cache_data::STARTS, usp_map_cache_data::RUNS,
        usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT, label, color);
  }

  uint16_t *buffer_;
  uint16_t colors_[27]{};
  bool ready_{};
  uint16_t cold_colors_[27]{};
  uint32_t cold_background_{}, cold_run_{}, cold_run_offset_{}, cold_step_pixels_{};
  uint8_t cold_label_{1};
  bool cold_pending_{};
};

}  // namespace usp_map_cache
