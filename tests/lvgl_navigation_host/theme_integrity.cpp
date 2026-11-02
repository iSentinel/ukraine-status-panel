#include "lvgl.h"
#include <cassert>
#include <cstdio>

/** Match ESPHome theme styles attached after LVGL's built-in theme. */
static void application_styles(lv_style_t &card, lv_style_t &label,
                               lv_style_t &button) {
  lv_style_init(&card);
  lv_style_set_pad_all(&card, 0);
  lv_style_set_text_color(&card, lv_color_hex(0xF8FAFC));
  lv_style_init(&label);
  lv_style_set_text_color(&label, lv_color_hex(0xF8FAFC));
  lv_style_init(&button);
  lv_style_set_pad_all(&button, 0);
  lv_style_set_shadow_width(&button, 0);
  lv_style_set_text_color(&button, lv_color_hex(0xF8FAFC));
}

/** Reproduce the clipping caused by an implicit twenty-pixel card padding. */
static lv_obj_t *make_card(lv_style_t *style) {
  auto *card = lv_obj_create(lv_screen_active());
  lv_obj_set_size(card, 524, 330);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  if (style) lv_obj_add_style(card, style, LV_PART_MAIN);
  auto *child = lv_label_create(card);
  lv_label_set_text(child, "History");
  lv_obj_set_pos(child, 24, 300);
  lv_obj_set_size(child, 420, 26);
  lv_obj_update_layout(card);
  return child;
}

/** Verify real resolved colors, geometry and button shadows. */
static void check_application(lv_style_t &card, lv_style_t &label,
                              lv_style_t &button) {
  auto *child = make_card(&card);
  auto *parent = lv_obj_get_parent(child);
  assert(lv_obj_get_style_pad_top(parent, LV_PART_MAIN) == 0);
  lv_area_t child_area{}, parent_area{};
  lv_obj_get_coords(child, &child_area);
  lv_obj_get_coords(parent, &parent_area);
  assert(child_area.y2 <= parent_area.y2);
  lv_obj_add_style(child, &label, LV_PART_MAIN);
  assert((lv_color_to_u32(lv_obj_get_style_text_color(child, LV_PART_MAIN)) &
          0xFFFFFFU) == 0xF8FAFCU);
  auto *control = lv_button_create(parent);
  lv_obj_add_style(control, &button, LV_PART_MAIN);
  assert(lv_obj_get_style_shadow_width(control, LV_PART_MAIN) == 0);
  const auto selector = static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  lv_obj_add_state(control, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(control, lv_color_hex(0x166534), selector);
  assert((lv_color_to_u32(lv_obj_get_style_bg_color(control, LV_PART_MAIN)) &
          0xFFFFFFU) == 0x166534U);
}

int main() {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  auto *before = make_card(nullptr);
  auto *parent = lv_obj_get_parent(before);
  const auto padding = lv_obj_get_style_pad_top(parent, LV_PART_MAIN);
  assert(padding > 0);
  lv_area_t child_area{}, parent_area{};
  lv_obj_get_coords(before, &child_area);
  lv_obj_get_coords(parent, &parent_area);
  assert(child_area.y2 > parent_area.y2);
  auto *dark = lv_theme_default_init(display, lv_color_hex(0x2563EB),
      lv_color_hex(0x263D57), true, LV_FONT_DEFAULT);
  lv_display_set_theme(display, dark);
  lv_style_t card, label, button;
  application_styles(card, label, button);
  check_application(card, label, button);
  lv_display_delete(display);
  lv_style_reset(&card);
  lv_style_reset(&label);
  lv_style_reset(&button);
  lv_deinit();
  std::printf("THEME_LVGL=PASS before_padding=%d before_clipped=1 "
              "after_padding=0 after_clipped=0 text=F8FAFC shadow=0\n", padding);
}
