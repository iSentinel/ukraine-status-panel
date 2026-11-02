#pragma once
#include "esphome/core/hal.h"
#include <cstdint>

namespace usp_lcd_navigation {

/** Bounded, explicitly armed evidence; it never changes input decisions. */
struct TouchTraceRecord {
  uint32_t duration{}, samples{}, raw_updates{}, raw_duration{};
  int16_t start_x{}, start_y{}, end_x{}, end_y{};
  int16_t raw_start_x{}, raw_start_y{}, raw_end_x{}, raw_end_y{};
  uint16_t pointers{}, bindings{};
  uint8_t page{}, action{};
  bool fullscreen{}, header{}, blocked{}, button{}, qa{}, consumed{};
};

class TouchTrace {
 public:
  using Sink = void (*)(const char *, const TouchTraceRecord &);
  void set_sink(Sink sink) { this->sink_ = sink; }

  void arm(uint32_t seconds, uint16_t pointers, uint16_t bindings) {
    this->raw_pressed_ = false;
    this->started_ = esphome::millis();
    this->limit_ = seconds <= 180 ? seconds * 1000 : 180000;
    this->record_.pointers = pointers;
    this->record_.bindings = bindings;
    this->event("ARM");
  }

  bool enabled() const {
    return this->sink_ != nullptr && this->limit_ != 0 &&
        esphome::millis() - this->started_ < this->limit_;
  }

  void raw_point(int16_t x, int16_t y) {
    if (!this->enabled()) return;
    if (!this->raw_pressed_) {
      this->raw_pressed_ = true;
      this->raw_started_ = esphome::millis();
      this->record_.raw_updates = 0;
      this->record_.raw_start_x = x;
      this->record_.raw_start_y = y;
    }
    ++this->record_.raw_updates;
    this->record_.raw_end_x = x;
    this->record_.raw_end_y = y;
  }

  void raw_release() {
    if (this->enabled() && this->raw_pressed_) {
      this->record_.raw_duration = esphome::millis() - this->raw_started_;
      this->event("GT911_RELEASE");
    }
    this->raw_pressed_ = false;
  }

  void press(int16_t x, int16_t y, uint8_t page, bool full, bool header,
             bool blocked, bool button, bool qa) {
    if (!this->enabled()) return;
    this->press_started_ = esphome::millis();
    this->record_.start_x = this->record_.end_x = x;
    this->record_.start_y = this->record_.end_y = y;
    this->record_.duration = this->record_.samples = this->record_.action = 0;
    this->record_.page = page;
    this->record_.fullscreen = full;
    this->record_.header = header;
    this->record_.blocked = blocked;
    this->record_.button = button;
    this->record_.qa = qa;
    this->record_.consumed = false;
    this->event("LVGL_PRESS");
  }

  void point(int16_t x, int16_t y) {
    if (!this->enabled()) return;
    this->record_.end_x = x;
    this->record_.end_y = y;
    this->record_.duration = esphome::millis() - this->press_started_;
    ++this->record_.samples;
  }

  void result(uint8_t action, bool consumed) {
    if (!this->enabled()) return;
    this->record_.action = action;
    this->record_.consumed = consumed;
  }

  void event(const char *stage) {
    if (this->enabled()) this->sink_(stage, this->record_);
  }

 private:
  Sink sink_{};
  TouchTraceRecord record_{};
  uint32_t started_{}, limit_{}, press_started_{}, raw_started_{};
  bool raw_pressed_{};
};

}  // namespace usp_lcd_navigation
