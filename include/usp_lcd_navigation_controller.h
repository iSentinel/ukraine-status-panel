#pragma once

#include "usp_swipe_navigation.h"
#include "usp_lcd_navigation_trace.h"
#include "usp_touch_event_time.h"
#include "esphome/core/hal.h"
#include <lvgl.h>
#include <cstring>

namespace usp_lcd_navigation {

inline void hidden(lv_obj_t *object, bool value) {
  if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == value) return;
  if (value) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

inline void label_text(lv_obj_t *label, const char *value) {
  if (label != nullptr && std::strcmp(lv_label_get_text(label), value) != 0)
    lv_label_set_text(label, value);
}

/** Apply a semantic colour only when it really changes. */
inline void label_color(lv_obj_t *label, uint32_t rgb) {
  if (label != nullptr &&
      !lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), lv_color_hex(rgb)))
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), LV_PART_MAIN);
}

inline void button_text(lv_obj_t *button, const char *value) {
  if (button != nullptr) label_text(lv_obj_get_child(button, 0), value);
}

/** Observe pointer events; the main loop performs page changes afterwards. */
class Controller {
 public:
  void attach(lv_obj_t *header, lv_obj_t *menu) {
    this->header_ = header;
    this->menu_ = menu;
    for (auto *input = lv_indev_get_next(nullptr); input != nullptr;
         input = lv_indev_get_next(input)) {
      if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER)
        this->bind(input);
    }
  }

  void bind(lv_indev_t *input) {
    lv_indev_add_event_cb(input, input_event_, LV_EVENT_ALL, this);
    ++this->bindings_;
  }

  void set_fullscreen(bool value) {
    if (this->fullscreen_ == value) return;
    this->fullscreen_ = value;
    this->gesture_ = usp_lcd_gesture::Gesture{};
    this->pending_ = usp_lcd_gesture::Action::NONE;
    this->event_time_.clear();
  }

  /** A visible top-layer modal blocks global gestures but not its own buttons. */
  void set_modal_root(lv_obj_t *root) {
    if (this->modal_root_ == root) return;
    this->modal_root_ = root;
    this->cancel_gesture_();
    this->event_time_.clear();
  }

  bool modal_open() const {
    return this->modal_root_ != nullptr &&
        !lv_obj_has_flag(this->modal_root_, LV_OBJ_FLAG_HIDDEN);
  }

  TouchTrace &trace() { return this->trace_; }

  void raw_touch_press(int16_t x, int16_t y, uint32_t now) {
    this->event_time_.press(now);
    this->trace_.raw_point(x, y);
  }

  void raw_touch_point(int16_t x, int16_t y, uint32_t now) {
    this->event_time_.point(now);
    this->trace_.raw_point(x, y);
  }

  void raw_touch_release(uint32_t now) {
    this->event_time_.release(now);
    this->trace_.raw_release();
  }

  /** Cancel an untrusted touch without turning its release into a button click. */
  void cancel_touch_fault() {
    this->remove_motion_();
    this->cancel_gesture_();
    this->event_time_.clear();
    this->trace_.raw_release();
    for (auto *input = lv_indev_get_next(nullptr); input != nullptr;
         input = lv_indev_get_next(input)) {
      if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) continue;
      lv_indev_reset(input, nullptr);
      lv_indev_wait_release(input);
    }
    this->input_ = nullptr;
  }

  void trace_for(uint32_t seconds) {
    uint16_t pointers = 0;
    for (auto *input = lv_indev_get_next(nullptr); input != nullptr;
         input = lv_indev_get_next(input)) {
      if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER) ++pointers;
    }
    this->trace_.arm(seconds, pointers, this->bindings_);
  }

  void set_page(uint8_t page) { this->page_ = page; }
  bool consumed() const { return this->gesture_.consumed(); }

  usp_lcd_gesture::Action take_action() {
    const auto result = this->pending_;
    this->pending_ = usp_lcd_gesture::Action::NONE;
    return result;
  }

  // Explicit QA input uses LVGL's normal pointer processor. It is created
  // only when requested and must be released by qa_end() after the check.
  void qa_touch(int16_t x, int16_t y, bool pressed) {
    if (this->qa_input_ == nullptr) {
      this->qa_input_ = lv_indev_create();
      lv_indev_set_type(this->qa_input_, LV_INDEV_TYPE_POINTER);
      lv_indev_set_display(this->qa_input_, lv_display_get_default());
      lv_indev_set_user_data(this->qa_input_, this);
      lv_indev_set_read_cb(this->qa_input_, [](lv_indev_t *input, lv_indev_data_t *data) {
        auto *self = static_cast<Controller *>(lv_indev_get_user_data(input));
        data->point = self->qa_point_;
        data->state = self->qa_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      });
      this->bind(this->qa_input_);
    }
    this->qa_point_ = {x, y};
    this->qa_pressed_ = pressed;
    lv_indev_read(this->qa_input_);
  }

  void qa_end() {
    if (this->qa_input_ == nullptr) return;
    this->qa_pressed_ = false;
    lv_indev_read(this->qa_input_);
    if (this->input_ == this->qa_input_) this->input_ = nullptr;
    lv_indev_delete(this->qa_input_);
    this->qa_input_ = nullptr;
  }

 private:
  static void input_event_(lv_event_t *event) {
    auto *self = static_cast<Controller *>(lv_event_get_user_data(event));
    auto *input = static_cast<lv_indev_t *>(lv_event_get_target(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
      self->press_(input, static_cast<lv_obj_t *>(lv_event_get_param(event)));
    } else if (code == LV_EVENT_RELEASED) {
      lv_point_t point;
      lv_indev_get_point(input, &point);
      self->trace_.point(point.x, point.y);
      uint32_t now;
      if (self->decision_time_(input, usp_touch_event::Kind::RELEASE, now)) {
        self->queue_(self->gesture_.release(point.x, point.y, now));
      } else {
        self->cancel_gesture_();
      }
      if (input != self->qa_input_) self->event_time_.clear();
      self->trace_.result(static_cast<uint8_t>(self->pending_), self->consumed());
      self->trace_.event("LVGL_RELEASE");
      self->remove_motion_();
    }
    // LVGL dispatches these input callbacks before the widget callbacks.
    // Suppress every release/click stage, so a map drag cannot select a region.
    if (self->consumed() && (code == LV_EVENT_RELEASED ||
        code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED)) {
      lv_indev_stop_processing(input);
    }
  }

  static void motion_event_(lv_event_t *event) {
    auto *self = static_cast<Controller *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) != LV_EVENT_PRESSING || self->input_ == nullptr)
      return;
    lv_point_t point;
    lv_indev_get_point(self->input_, &point);
    self->trace_.point(point.x, point.y);
    uint32_t now;
    if (!self->decision_time_(self->input_, usp_touch_event::Kind::POINT, now)) {
      self->cancel_gesture_();
      return;
    }
    self->queue_(self->gesture_.sample(point.x, point.y, now));
  }

  void press_(lv_indev_t *input, lv_obj_t *target) {
    this->remove_motion_();
    lv_point_t point;
    lv_indev_get_point(input, &point);
    const bool header = target != nullptr && target == this->header_;
    const bool button = this->button_in_path_(target);
    const bool blocked = this->modal_open() || (!header && this->blocked_(target));
    this->trace_.press(point.x, point.y, this->page_, this->fullscreen_,
                       header, blocked, button, input == this->qa_input_);
    uint32_t now;
    if (!this->decision_time_(input, usp_touch_event::Kind::PRESS, now)) {
      this->cancel_gesture_();
      return;
    }
    this->input_ = input;
    this->target_ = target;
    this->gesture_.press(point.x, point.y, now, this->fullscreen_,
                         blocked && !button, header, !button);
    if (this->fullscreen_ && (!blocked || button) && target != nullptr) {
      lv_obj_add_event_cb(target, motion_event_, LV_EVENT_PRESSING, this);
    }
  }

  bool button_in_path_(lv_obj_t *target) const {
#if LV_USE_BUTTON
    for (auto *obj = target; obj != nullptr; obj = lv_obj_get_parent(obj)) {
      if (lv_obj_check_type(obj, &lv_button_class)) return true;
    }
#endif
    return false;
  }

  bool blocked_(lv_obj_t *target) const {
    if (this->page_ == 8 || target == nullptr) return true;
    if (this->menu_ != nullptr &&
        !lv_obj_has_flag(this->menu_, LV_OBJ_FLAG_HIDDEN)) return true;
    for (auto *obj = target; obj != nullptr; obj = lv_obj_get_parent(obj)) {
      if (this->control_(obj)) return true;
      // Clipped content is not an interactive scroll area.
      if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) &&
          lv_obj_get_parent(obj) != nullptr &&
          (lv_obj_get_scroll_top(obj) > 0 || lv_obj_get_scroll_bottom(obj) > 0 ||
           lv_obj_get_scroll_left(obj) > 0 || lv_obj_get_scroll_right(obj) > 0))
        return true;
    }
    return false;
  }

  bool control_(lv_obj_t *obj) const {
#if LV_USE_BUTTON
    if (lv_obj_check_type(obj, &lv_button_class)) return true;
#endif
#if LV_USE_SLIDER
    if (lv_obj_check_type(obj, &lv_slider_class)) return true;
#endif
#if LV_USE_DROPDOWN
    if (lv_obj_check_type(obj, &lv_dropdown_class)) return true;
#endif
#if LV_USE_KEYBOARD
    if (lv_obj_check_type(obj, &lv_keyboard_class)) return true;
#endif
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(obj, &lv_textarea_class)) return true;
#endif
#if LV_USE_MSGBOX
    if (lv_obj_check_type(obj, &lv_msgbox_class)) return true;
#endif
    return false;
  }

  bool decision_time_(lv_indev_t *input, usp_touch_event::Kind kind,
                      uint32_t &now) const {
    if (input == this->qa_input_) {
      now = esphome::millis();
      return true;
    }
    return this->event_time_.get(kind, now);
  }

  void cancel_gesture_() {
    this->gesture_ = usp_lcd_gesture::Gesture{};
    this->pending_ = usp_lcd_gesture::Action::NONE;
  }

  void remove_motion_() {
    if (this->target_ != nullptr) {
      lv_obj_remove_event_cb_with_user_data(this->target_, motion_event_, this);
      this->target_ = nullptr;
    }
  }

  void queue_(usp_lcd_gesture::Action action) {
    if (action != usp_lcd_gesture::Action::NONE) {
      this->pending_ = action;
      this->trace_.result(static_cast<uint8_t>(action), this->consumed());
      this->trace_.event("ACTION");
    }
  }

  TouchTrace trace_{};
  usp_touch_event::EventTime event_time_{};
  uint16_t bindings_{};
  usp_lcd_gesture::Gesture gesture_{};
  usp_lcd_gesture::Action pending_{usp_lcd_gesture::Action::NONE};
  lv_obj_t *header_{};
  lv_obj_t *menu_{};
  lv_obj_t *modal_root_{};
  lv_obj_t *target_{};
  lv_indev_t *input_{};
  uint8_t page_{};
  bool fullscreen_{};
  lv_indev_t *qa_input_{};
  lv_point_t qa_point_{};
  bool qa_pressed_{};
};

inline Controller &controller() {
  static Controller instance;
  return instance;
}

}  // namespace usp_lcd_navigation
