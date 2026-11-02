#pragma once

#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdint>

namespace usp_transition {

// Counts completed render cycles, not physical LCD scan-out frames.
// The observer is attached only during an explicit, bounded QA request.
struct Sample {
  uint32_t frames{}, first_ms{}, end_ms{}, loaded_ms{};
  uint32_t render_max_ms{}, gap_max_ms{};
  size_t heap_before{}, heap_min{}, psram_before{}, psram_min{};
  bool loaded{}, complete{};
};

class Probe {
 public:
  bool begin(lv_display_t *display, lv_obj_t *target, int page, bool animated) {
    if (active_ || display == nullptr || target == nullptr ||
        lv_display_get_screen_active(display) == target)
      return false;
    display_ = display;
    target_ = target;
    page_ = page;
    animated_ = animated;
    sample_ = Sample{};
    started_ = esphome::millis();
    render_open_ = false;
    active_ = true;
    sample_.heap_before = sample_.heap_min = free_internal_();
    sample_.psram_before = sample_.psram_min = free_psram_();
    lv_display_add_event_cb(display_, display_event_, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(target_, loaded_event_, LV_EVENT_SCREEN_LOADED, this);
    return true;
  }

  void finish() {
    if (!active_) return;
    observe_memory_();
    lv_display_remove_event_cb_with_user_data(display_, display_event_, this);
    lv_obj_remove_event_cb_with_user_data(target_, loaded_event_, this);
    active_ = false;
    report_();
    display_ = nullptr;
    target_ = nullptr;
  }

  bool active() const { return active_; }
  const Sample &sample() const { return sample_; }

 private:
  static size_t free_internal_() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  static size_t free_psram_() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void observe_memory_() {
    sample_.heap_min = std::min(sample_.heap_min, free_internal_());
    sample_.psram_min = std::min(sample_.psram_min, free_psram_());
  }

  static void loaded_event_(lv_event_t *event) {
    auto *self = static_cast<Probe *>(lv_event_get_user_data(event));
    if (!self->active_ || self->sample_.loaded) return;
    self->sample_.loaded = true;
    self->sample_.loaded_ms = esphome::millis() - self->started_;
  }

  static void display_event_(lv_event_t *event) {
    auto *self = static_cast<Probe *>(lv_event_get_user_data(event));
    if (!self->active_ || self->sample_.complete) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_RENDER_START) {
      self->render_started_ = esphome::millis();
      self->render_open_ = true;
      self->observe_memory_();
    } else if (code == LV_EVENT_REFR_READY && self->render_open_) {
      self->render_ready_();
    }
  }

  void render_ready_() {
    const uint32_t now = esphome::millis();
    const uint32_t elapsed = now - started_;
    sample_.render_max_ms = std::max(sample_.render_max_ms, now - render_started_);
    if (sample_.frames == 0) sample_.first_ms = elapsed;
    const uint32_t gap = sample_.frames == 0 ? elapsed : elapsed - sample_.end_ms;
    sample_.gap_max_ms = std::max(sample_.gap_max_ms, gap);
    ++sample_.frames;
    sample_.end_ms = elapsed;
    sample_.complete = sample_.loaded;
    render_open_ = false;
    observe_memory_();
  }

  void report_() const {
    ESP_LOGI("usp.transition",
      "TRANSITION_PROFILE page=%d animated=%u requested_ms=350 frames=%lu "
      "first_ms=%lu end_ms=%lu loaded_ms=%lu render_max_ms=%lu gap_max_ms=%lu "
      "loaded=%u complete=%u heap_before=%u heap_min=%u psram_before=%u psram_min=%u",
      page_, animated_, static_cast<unsigned long>(sample_.frames),
      static_cast<unsigned long>(sample_.first_ms), static_cast<unsigned long>(sample_.end_ms),
      static_cast<unsigned long>(sample_.loaded_ms),
      static_cast<unsigned long>(sample_.render_max_ms),
      static_cast<unsigned long>(sample_.gap_max_ms), sample_.loaded, sample_.complete,
      static_cast<unsigned>(sample_.heap_before), static_cast<unsigned>(sample_.heap_min),
      static_cast<unsigned>(sample_.psram_before), static_cast<unsigned>(sample_.psram_min));
  }

  lv_display_t *display_{};
  lv_obj_t *target_{};
  Sample sample_{};
  uint32_t started_{}, render_started_{};
  int page_{};
  bool active_{}, animated_{}, render_open_{};
};

inline Probe &probe() {
  static Probe instance;
  return instance;
}

}  // namespace usp_transition
