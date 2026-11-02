#include "usp_settings_sections.h"
#include "usp_lcd_navigation_controller.h"
#include <cassert>
#include <cstdio>

uint32_t navigation_test_tick = 1000;

namespace {
void draw(lv_display_t *, const lv_area_t *, uint8_t *) {}

void clicked(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}

lv_obj_t *make_panel(unsigned *clicks) {
  auto *panel = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(panel);
  lv_obj_set_pos(panel, 280, 84);
  lv_obj_set_size(panel, 716, 438);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  auto *control = lv_button_create(panel);
  lv_obj_set_pos(control, 20, 80);
  lv_obj_set_size(control, 640, 54);
  lv_obj_add_event_cb(control, clicked, LV_EVENT_CLICKED, clicks);
  return panel;
}

void tap() {
  usp_lcd_navigation::Controller controller;
  controller.set_page(5);
  controller.set_fullscreen(false);
  lv_obj_update_layout(lv_screen_active());
  controller.qa_touch(600, 190, true);
  navigation_test_tick += 60;
  lv_tick_inc(60);
  controller.qa_touch(600, 190, false);
  navigation_test_tick += 60;
  lv_tick_inc(60);
  controller.qa_end();
}

void check(unsigned selection) {
  unsigned clicks[usp_settings_sections::COUNT]{};
  lv_obj_t *panels[usp_settings_sections::COUNT]{};
  lv_obj_t *buttons[usp_settings_sections::COUNT]{};
  for (unsigned index = 0; index < usp_settings_sections::COUNT; ++index) {
    panels[index] = make_panel(&clicks[index]);
    buttons[index] = lv_button_create(lv_screen_active());
  }
  usp_settings_sections::apply(panels, buttons, selection);
  unsigned visible = 0;
  for (unsigned index = 0; index < usp_settings_sections::COUNT; ++index) {
    const bool active = index == usp_settings_sections::sanitize(selection);
    assert(lv_obj_has_flag(panels[index], LV_OBJ_FLAG_HIDDEN) != active);
    assert(lv_obj_get_style_border_width(buttons[index], LV_PART_MAIN) ==
           (active ? 2 : 1));
    if (!lv_obj_has_flag(panels[index], LV_OBJ_FLAG_HIDDEN)) ++visible;
  }
  assert(visible == 1);
  tap();
  for (unsigned index = 0; index < usp_settings_sections::COUNT; ++index) {
    assert(clicks[index] == (index == usp_settings_sections::sanitize(selection) ? 1U : 0U));
    lv_obj_delete(panels[index]);
    lv_obj_delete(buttons[index]);
  }
}
}  // namespace

int main() {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  lv_display_set_flush_cb(display, draw);
  for (unsigned index = 0; index < 256; ++index) {
    check(index);
    for (unsigned language = 0; language < 3; ++language) {
      assert(usp_settings_sections::name(language, index)[0] != '\0');
      assert(usp_settings_sections::network(language, false)[0] != '\0');
      assert(usp_settings_sections::network(language, true)[0] != '\0');
    }
  }
  lv_display_delete(display);
  lv_deinit();
  std::puts("SETTINGS_SECTIONS_LVGL=PASS selections256 exclusive6 hidden_clicks0 fallbacks");
}
