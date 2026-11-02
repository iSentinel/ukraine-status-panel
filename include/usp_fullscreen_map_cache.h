#pragma once

#include <lvgl.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "usp_alert_palette_0_115_114.h"
#include "usp_alert_region_colors.h"
#include "usp_map_cache_renderer.h"
#include "usp_map_present_gate.h"

namespace usp_fullscreen_map_cache {

inline uint32_t region_color_hex(int state, uint8_t palette) {
  return usp_alert_region_colors::color_hex(state, palette);
}

// Preserve the existing batching if the optional PSRAM image cannot be used.
inline void update_fallback(lv_obj_t *const *regions, const int *states, uint8_t palette, size_t batch) {
  static uint8_t cache[25]{};
  static bool ready = false;
  static uint8_t previous_palette = 0xFF;
  palette = usp_alert_palette::sanitize_mode(palette);
  if (batch == 0 && (!ready || previous_palette != palette)) {
    std::fill_n(cache, 25, static_cast<uint8_t>(0xFF));
    previous_palette = palette;
    ready = true;
  }
  const size_t begin = batch == 0 ? 0 : 1 + (batch - 1) * 3;
  const size_t end = std::min(begin + (batch == 0 ? 1U : 3U), static_cast<size_t>(25));
  uint8_t changed = 0;
  for (size_t index = begin; index < end; ++index) {
    const uint8_t state = states[index];
    if (cache[index] == state) continue;
    cache[index] = state;
    ++changed;
    lv_obj_set_style_image_recolor(regions[index], lv_color_hex(region_color_hex(state, palette)), LV_PART_MAIN);
  }
  if (changed != 0)
    ESP_LOGD("usp.map", "Fullscreen map batch=%u changed=%u", static_cast<unsigned>(batch), changed);
}

// One reusable PSRAM image. Existing image objects remain as the low-memory fallback.
class Cache {
 public:
  static Cache &instance() {
    static Cache cache;
    return cache;
  }
  bool active() const { return image_ != nullptr && renderer_.ready(); }
  bool cold_pending() const { return image_ != nullptr && renderer_.cold_pending(); }

  /** Keep first-frame work cooperative and publish only completed pixels. */
  bool refresh(lv_obj_t *parent, lv_obj_t *const *originals, size_t count,
               const int *states, uint8_t palette) {
    const uint32_t started = esphome::millis();
    if (count > ORIGINAL_CAPACITY || !this->prepare_(parent)) return false;
    std::copy_n(originals, count, originals_);
    original_count_ = count;
    uint16_t colors[27];
    this->make_colors_(states, palette, colors);
    if (!renderer_.ready()) {
      if (!renderer_.cold_pending()) {
        requested_ms_ = started;
        cold_started_ms_ = started;
      }
      renderer_.cold_begin(colors);
      ++request_sequence_;
      ESP_LOGI("usp.map.cache", "FULLSCREEN_CACHE_COLD_BEGIN seq=%u setup_ms=%u bytes=%u",
               static_cast<unsigned>(request_sequence_),
               static_cast<unsigned>(esphome::millis() - started),
               static_cast<unsigned>(BUFFER_BYTES));
      return false;
    }
    lv_image_cache_drop(&descriptor_);
    const bool changed = renderer_.update(colors);
    requested_ms_ = started;
    ++request_sequence_;
    this->show_ready_(changed);
    this->log_update_(changed, started);
    return true;
  }

  /** Complete one bounded CPU slice; unfinished pixels stay hidden. */
  void cold_step(uint32_t pixel_budget) {
    if (!this->cold_pending()) return;
    const uint32_t started = esphome::millis();
    const bool complete = renderer_.cold_step(pixel_budget);
    if (complete) {
      lv_image_cache_drop(&descriptor_);
      this->show_ready_(true);
    }
    cold_max_slice_ms_ = std::max(cold_max_slice_ms_, esphome::millis() - started);
    cold_pixels_ += renderer_.cold_step_pixels();
    ++cold_steps_;
    if (complete) {
      ESP_LOGI("usp.map.cache",
               "FULLSCREEN_CACHE_COLD_READY seq=%u steps=%u pixels=%u max_slice_ms=%u elapsed_ms=%u",
               static_cast<unsigned>(request_sequence_), static_cast<unsigned>(cold_steps_),
               static_cast<unsigned>(cold_pixels_), static_cast<unsigned>(cold_max_slice_ms_),
               static_cast<unsigned>(esphome::millis() - cold_started_ms_));
      this->log_update_(true, cold_started_ms_);
    }
  }

 private:
  static constexpr size_t ORIGINAL_CAPACITY = 67;

  /** Copy semantic colors before a yielded first-frame transaction. */
  void make_colors_(const int *states, uint8_t palette, uint16_t (&colors)[27]) {
    colors[0] = usp_map_cache::rgb565(0x07111F);
    colors[26] = usp_map_cache::rgb565(0x91A8B2);
    for (size_t index = 0; index < 25; ++index)
      colors[index + 1] = usp_map_cache::rgb565(region_color_hex(states[index], palette));
  }

  /** Replace original masks only after every cached pixel is initialized. */
  void show_ready_(bool changed) {
    if (changed) lv_obj_invalidate(image_);
    lv_obj_clear_flag(image_, LV_OBJ_FLAG_HIDDEN);
    for (size_t index = 0; index < original_count_; ++index)
      lv_obj_add_flag(originals_[index], LV_OBJ_FLAG_HIDDEN);
    present_gate_.arm();
  }

  /** Preserve the existing elapsed-to-present diagnostic contract. */
  void log_update_(bool changed, uint32_t started) {
    ESP_LOGI("usp.map.cache", "FULLSCREEN_CACHE_UPDATE seq=%u changed=%s prepare_ms=%u bytes=%u",
             static_cast<unsigned>(request_sequence_), changed ? "YES" : "NO",
             static_cast<unsigned>(esphome::millis() - started), static_cast<unsigned>(BUFFER_BYTES));
  }

  static constexpr uint32_t BUFFER_BYTES = usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT * 2U;

  bool prepare_(lv_obj_t *parent) {
    if (image_ != nullptr)
      return true;
    if (allocation_failed_)
      return false;
    buffer_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(
        64, BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer_ == nullptr) {
      allocation_failed_ = true;
      ESP_LOGW("usp.map.cache", "PSRAM unavailable; keeping the original map images");
      return false;
    }
    renderer_ = usp_map_cache::Renderer(buffer_);
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor_.header.w = usp_map_cache_data::WIDTH;
    descriptor_.header.h = usp_map_cache_data::HEIGHT;
    descriptor_.header.stride = usp_map_cache_data::WIDTH * 2;
    descriptor_.data_size = BUFFER_BYTES;
    descriptor_.data = reinterpret_cast<const uint8_t *>(buffer_);
    image_ = lv_image_create(parent);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, 63, 12);
    lv_obj_clear_flag(image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(image_, LV_OBJ_FLAG_SCROLLABLE);
    // Keep the existing Kyiv marker and other overlays above the cached image.
    lv_obj_move_to_index(image_, 0);
    auto *display = lv_obj_get_display(parent);
    lv_display_add_event_cb(display, event_, LV_EVENT_RENDER_START, this);
    lv_display_add_event_cb(display, event_, LV_EVENT_REFR_READY, this);
    return true;
  }

  static void event_(lv_event_t *event) {
    auto *self = static_cast<Cache *>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (!self->present_gate_.consume(
            lv_obj_get_screen(self->image_) == lv_screen_active(),
            code == LV_EVENT_RENDER_START, code == LV_EVENT_REFR_READY))
      return;
    ESP_LOGI("usp.map.cache", "FULLSCREEN_CACHE_PRESENT seq=%u map_refresh_ms=%u",
             static_cast<unsigned>(self->request_sequence_),
             static_cast<unsigned>(esphome::millis() - self->requested_ms_));
  }

  lv_obj_t *originals_[ORIGINAL_CAPACITY]{};
  size_t original_count_{};
  uint32_t cold_started_ms_{}, cold_max_slice_ms_{}, cold_pixels_{}, cold_steps_{};
  uint16_t *buffer_{};
  usp_map_cache::Renderer renderer_{nullptr};
  lv_image_dsc_t descriptor_{};
  lv_obj_t *image_{};
  uint32_t requested_ms_{}, request_sequence_{};
  bool allocation_failed_{};
  usp_map_present_gate::Gate present_gate_{};
};

}  // namespace usp_fullscreen_map_cache
