#include "usp_lcd_navigation_controller.h"
#include "weather-touch-rects.h"
#include <cassert>
#include <cstdio>

uint32_t navigation_test_tick = 1000;

/** Advance the same monotonic time used by LVGL and the controller. */
void feed(usp_lcd_navigation::Controller &controller, int x, int y, bool down) {
  controller.qa_touch(x, y, down);
  navigation_test_tick += 60;
  lv_tick_inc(60);
}

/** Count real LVGL click dispatch; firmware actions are never invoked here. */
void clicked(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}

/** Build the actual configured rectangle inside its actual parent bounds. */
lv_obj_t *control(const Rect &rect, unsigned *clicks) {
  auto *parent = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(parent);
  lv_obj_set_pos(parent, rect.px, rect.py);
  lv_obj_set_size(parent, rect.pw, rect.ph);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  auto *button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, rect.x - rect.px, rect.y - rect.py);
  lv_obj_set_size(button, rect.w, rect.h);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, clicks);
  auto *label = lv_label_create(button);
  lv_label_set_text(label, "CONTROL");
  lv_obj_center(label);
  lv_obj_update_layout(lv_screen_active());
  return parent;
}

/** Taps reach each edge while a point just outside cannot click the control. */
void taps(const Rect &r, usp_lcd_navigation::Controller &controller,
          unsigned &clicks) {
  const int points[5][2] = {
      {r.x+r.w/2, r.y+r.h/2}, {r.x+r.w/2, r.y+1},
      {r.x+r.w/2, r.y+r.h-1}, {r.x+1, r.y+r.h/2},
      {r.x+r.w-1, r.y+r.h/2}};
  for (const auto &point : points) {
    const unsigned before = clicks;
    feed(controller, point[0], point[1], true);
    feed(controller, point[0], point[1], false);
    assert(clicks == before+1);
    assert(controller.take_action() == usp_lcd_gesture::Action::NONE);
  }
  const unsigned before = clicks;
  feed(controller, r.x+r.w/2, r.y+r.h, true);
  feed(controller, r.x+r.w/2, r.y+r.h, false);
  assert(clicks == before);
  assert(controller.take_action() == usp_lcd_gesture::Action::NONE);
}

/** A drag starting on a button cancels its click without switching pages. */
void button_drag(const Rect &r, usp_lcd_navigation::Controller &controller,
           unsigned &clicks) {
  const unsigned before = clicks;
  const int x = r.x+r.w-4, y = r.y+r.h/2;
  feed(controller, x, y, true);
  feed(controller, x-70, y, true);
  feed(controller, x-140, y, true);
  feed(controller, x-140, y, false);
  assert(controller.take_action() == usp_lcd_gesture::Action::NONE);
  assert(clicks == before);
}

/** Background swipes retain global navigation outside interactive controls. */
void background_swipe(const Rect &r, usp_lcd_navigation::Controller &controller,
                      unsigned &clicks) {
  const unsigned before = clicks;
  const int x = r.px+r.pw-10, y = r.py+r.ph-4;
  feed(controller, x, y, true);
  feed(controller, x-70, y, true);
  feed(controller, x-140, y, true);
  feed(controller, x-140, y, false);
  assert(controller.take_action() == usp_lcd_gesture::Action::NEXT);
  assert(clicks == before);
}

/** Exercise the configured hit area with the production gesture controller. */
void check(const Rect &rect, bool fullscreen) {
  unsigned clicks = 0;
  auto *parent = control(rect, &clicks);
  {
    usp_lcd_navigation::Controller controller;
    controller.set_page(7);
    controller.set_fullscreen(fullscreen);
    taps(rect, controller, clicks);
    if (fullscreen) {
      button_drag(rect, controller, clicks);
      background_swipe(rect, controller, clicks);
    }
    controller.qa_end();
  }
  lv_obj_delete(parent);
}

int main() {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
  for (const auto &rect : RECTS) {
    assert(rect.w >= 48 && rect.h >= 48);
    check(rect, false);
    check(rect, true);
  }
  lv_display_delete(display);
  lv_deinit();
  std::printf("WEATHER_TOUCH_LVGL=PASS controls=%u tap_cases=%u "
              "outside_cases=%u button_drags_cancel=%u background_swipes=%u physical_touch=not_tested\n",
              unsigned(RECT_COUNT), unsigned(RECT_COUNT*10),
              unsigned(RECT_COUNT*2), unsigned(RECT_COUNT), unsigned(RECT_COUNT));
}
