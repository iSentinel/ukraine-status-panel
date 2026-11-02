#include "usp_button_label_layout.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

/** Recreate ESPHome's automatic text child in a button with zero padding. */
static lv_obj_t *button(bool centered) {
  auto *control = lv_button_create(lv_screen_active());
  auto *label = lv_label_create(control);
  if (centered) lv_obj_center(label);
  lv_obj_set_style_pad_all(control, 0, LV_PART_MAIN);
  lv_obj_set_size(control, 200, 54);
  lv_label_set_text(label, "Save");
  lv_obj_update_layout(control);
  return control;
}

/** Keep text centered after translation, checked styling and resizing. */
static void changes(lv_obj_t *control) {
  auto *label = lv_obj_get_child(control, 0);
  for (const auto *text : {"Save", "Зберегти", "Сохранить", "RESTART"}) {
    lv_label_set_text(label, text);
    for (int width : {180, 200, 420}) {
      lv_obj_set_size(control, width, 54);
      lv_obj_update_layout(control);
      const auto value = usp_button_label_layout::inspect(control);
      assert(value.valid && value.inside);
      assert(std::abs(value.error_x) <= 1 && std::abs(value.error_y) <= 1);
    }
  }
}

int main() {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  auto *before = button(false);
  const auto old = usp_button_label_layout::inspect(before);
  assert(old.valid && (std::abs(old.error_x) > 1 || std::abs(old.error_y) > 1));
  lv_obj_add_flag(before, LV_OBJ_FLAG_HIDDEN);
  auto *after = button(true);
  changes(after);
  auto report = usp_button_label_layout::audit(lv_screen_active());
  assert(report.total == 1 && report.offcenter == 0 && report.outside == 0);
  std::printf("BUTTON_ALIGNMENT=PASS before_x2=%d before_y2=%d "
              "after_offcenter=0 locales=3 resize_cases=12\n", old.error_x, old.error_y);
  lv_display_delete(display);
  lv_deinit();
}
