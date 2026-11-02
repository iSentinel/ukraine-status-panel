#pragma once

#include <cstdint>

namespace usp_touch_event {

enum class Kind : uint8_t { PRESS, POINT, RELEASE };

/** Preserve physical touch timing until LVGL consumes the stroke. */
class EventTime {
 public:
  void press(uint32_t now) {
    this->press_ = this->point_ = now;
    this->have_press_ = this->have_point_ = true;
    this->have_release_ = false;
    this->active_ = true;
  }

  void point(uint32_t now) {
    if (!this->active_) return;
    this->point_ = now;
    this->have_point_ = true;
  }

  void release(uint32_t now) {
    if (!this->have_press_) return;
    this->release_ = now;
    this->have_release_ = true;
    this->active_ = false;
  }

  bool get(Kind kind, uint32_t &now) const {
    if (kind == Kind::PRESS && this->have_press_) now = this->press_;
    else if (kind == Kind::POINT && this->have_point_) now = this->point_;
    else if (kind == Kind::RELEASE && this->have_release_) now = this->release_;
    else return false;
    return true;
  }

  void clear() {
    this->have_press_ = this->have_point_ = this->have_release_ = false;
    this->active_ = false;
  }

 private:
  uint32_t press_{};
  uint32_t point_{};
  uint32_t release_{};
  bool have_press_{};
  bool have_point_{};
  bool have_release_{};
  bool active_{};
};

}  // namespace usp_touch_event
