#pragma once

#include <lvgl.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "usp_alert_palette_0_115_114.h"
#include "usp_alert_region_colors.h"
#include "usp_dashboard_map_renderer.h"
#include "usp_map_present_gate.h"

namespace usp_dashboard_map_cache {

inline uint32_t region_color_hex(int state, uint8_t palette) {
  return usp_alert_region_colors::color_hex(state, palette);
}

class Cache {
 public:
  static Cache &instance() {
    static Cache cache;
    return cache;
  }

  bool active() const { return image_ != nullptr; }

  /** Share existing pixels with Clock; never allocate another map buffer. */
  bool mirror_to(lv_obj_t *target, const int *states, uint8_t palette,
                 bool *changed = nullptr) {
    if (changed != nullptr) *changed = false;
    this->mirror_ = target;
    if (!this->active() || target == nullptr) return false;
    uint16_t colors[27];
    this->colors_(colors, states, palette);
    usp_dashboard_map_renderer::DirtyArea dirty{};
    if (renderer_.update(colors, &dirty)) {
      lv_image_cache_drop(&descriptor_);
      this->invalidate_(image_, dirty);
      this->invalidate_(target, dirty);
      if (changed != nullptr) *changed = true;
    }
    if (lv_image_get_src(target) != &descriptor_)
      lv_image_set_src(target, &descriptor_);
    return true;
  }


  bool refresh(
      lv_obj_t *parent,
      lv_obj_t *const *originals,
      size_t count,
      const int *states,
      uint8_t palette) {
    if (!this->prepare_(parent))
      return false;
    const uint32_t started = esphome::millis();
    uint16_t colors[27];
    this->colors_(colors, states, palette);
    usp_dashboard_map_renderer::DirtyArea dirty{};
    const bool changed = renderer_.update(colors, &dirty);
    if (!changed)
      return true;

    // Dropping the LVGL image cache or re-applying HIDDEN to the legacy map
    // objects invalidates the Home surface. Do those operations only when the
    // RGB565 pixels actually changed; a repeated Home entry with identical
    // alert states must be a true render no-op.
    lv_image_cache_drop(&descriptor_);
    this->invalidate_(image_, dirty);
    this->invalidate_(this->mirror_, dirty);
    for (size_t index = 0; index < count; ++index)
      lv_obj_add_flag(originals[index], LV_OBJ_FLAG_HIDDEN);
    present_gate_.arm();
    requested_ms_ = started;
    ++request_sequence_;
    ESP_LOGI(
        "usp.map.cache",
        "DASHBOARD_CACHE_UPDATE seq=%u changed=YES prepare_ms=%u bytes=%u",
        static_cast<unsigned>(request_sequence_),
        static_cast<unsigned>(esphome::millis() - started),
        static_cast<unsigned>(BUFFER_BYTES));
    return true;
  }

 private:
  static void invalidate_(
      lv_obj_t *target, const usp_dashboard_map_renderer::DirtyArea &dirty) {
    if (target == nullptr || !dirty.changed) return;
    if (dirty.full) {
      lv_obj_invalidate(target);
      return;
    }
    lv_area_t coords{};
    lv_obj_get_coords(target, &coords);
    lv_area_t area{
        coords.x1 + dirty.x1, coords.y1 + dirty.y1,
        coords.x1 + dirty.x2, coords.y1 + dirty.y2};
    lv_obj_invalidate_area(target, &area);
  }

  static void colors_(uint16_t (&colors)[27], const int *states, uint8_t palette) {
    colors[0] = usp_dashboard_map_renderer::rgb565(0x0C1828);
    colors[26] = usp_dashboard_map_renderer::rgb565(0x91A8B2);
    for (size_t index = 0; index < 25; ++index) {
      colors[index + 1] = usp_dashboard_map_renderer::rgb565(
          region_color_hex(states[index], palette));
    }
  }

  static constexpr uint32_t BUFFER_BYTES =
      usp_dashboard_map_cache_data::WIDTH *
      usp_dashboard_map_cache_data::HEIGHT * 2U;

  bool prepare_(lv_obj_t *parent) {
    if (image_ != nullptr)
      return true;
    if (allocation_failed_)
      return false;
    buffer_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(
        64, BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer_ == nullptr) {
      allocation_failed_ = true;
      ESP_LOGW(
          "usp.map.cache",
          "Dashboard PSRAM cache unavailable; keeping original map images");
      return false;
    }
    renderer_ = usp_dashboard_map_renderer::Renderer(buffer_);
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor_.header.w = usp_dashboard_map_cache_data::WIDTH;
    descriptor_.header.h = usp_dashboard_map_cache_data::HEIGHT;
    descriptor_.header.stride = usp_dashboard_map_cache_data::WIDTH * 2;
    descriptor_.data_size = BUFFER_BYTES;
    descriptor_.data = reinterpret_cast<const uint8_t *>(buffer_);
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, 24, 63);
    lv_obj_clear_flag(image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(image_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(image_, 0);
    auto *display = lv_obj_get_display(parent);
    lv_display_add_event_cb(display, event_, LV_EVENT_RENDER_START, this);
    lv_display_add_event_cb(display, event_, LV_EVENT_REFR_READY, this);
    ESP_LOGI(
        "usp.map.cache",
        "Dashboard PSRAM cache ready bytes=%u",
        static_cast<unsigned>(BUFFER_BYTES));
    return true;
  }

  static void event_(lv_event_t *event) {
    auto *self = static_cast<Cache *>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (!self->present_gate_.consume(
            lv_obj_get_screen(self->image_) == lv_screen_active(),
            code == LV_EVENT_RENDER_START, code == LV_EVENT_REFR_READY))
      return;
    ESP_LOGI(
        "usp.map.cache",
        "DASHBOARD_CACHE_PRESENT seq=%u map_refresh_ms=%u",
        static_cast<unsigned>(self->request_sequence_),
        static_cast<unsigned>(esphome::millis() - self->requested_ms_));
  }

  uint16_t *buffer_{};
  usp_dashboard_map_renderer::Renderer renderer_{nullptr};
  lv_image_dsc_t descriptor_{};
  lv_obj_t *image_{};
  lv_obj_t *mirror_{};
  uint32_t requested_ms_{};
  uint32_t request_sequence_{};
  bool allocation_failed_{};
  usp_map_present_gate::Gate present_gate_{};
};

}  // namespace usp_dashboard_map_cache
