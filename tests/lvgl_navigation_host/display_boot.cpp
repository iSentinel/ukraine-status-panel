#include <lvgl.h>
#include <cassert>
#include <cstdio>

/** Count the layout requests caused solely by late screen resizing. */
static void count_request(lv_event_t *event) {
  auto *count = static_cast<unsigned *>(lv_event_get_user_data(event));
  ++*count;
}

/** Populate eleven screens before applying the final panel dimensions. */
static unsigned check_display(bool native) {
  auto *display = lv_display_create(native ? 1024 : 240, native ? 600 : 240);
  lv_display_set_default(display);
  unsigned requests = 0;
  lv_display_add_event_cb(display, count_request, LV_EVENT_REFR_REQUEST, &requests);
  for (unsigned page = 0; page < 11; ++page) {
    auto *screen = lv_obj_create(nullptr);
    for (unsigned item = 0; item < 60; ++item) {
      auto *label = lv_label_create(screen);
      lv_label_set_text(label, "Panel");
      lv_obj_set_pos(label, item * 3, item * 2);
    }
  }
  requests = 0;
  lv_display_set_resolution(display, 1024, 600);
  const unsigned result = requests;
  lv_display_delete(display);
  return result;
}

/** This tests real LVGL event work; it does not predict hardware timing. */
int main() {
  lv_init();
  const unsigned placeholder = check_display(false);
  const unsigned native = check_display(true);
  assert(placeholder >= 660);
  assert(native == 0);
  std::printf("LVGL_NATIVE_BOOT=PASS placeholder_requests=%u native_requests=%u\n",
              placeholder, native);
  lv_deinit();
}
