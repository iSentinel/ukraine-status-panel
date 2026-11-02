#include "lvgl.h"
// Mirror ESPHome's overload before including the project helper.
namespace esphome::image { class Image; }
namespace esphome::lvgl { void lv_image_set_src(lv_obj_t *,image::Image *); }
using namespace esphome::lvgl;
#include "usp_spotify_artwork_lcd.h"
#include <array>
#include <cassert>
#include <cstdio>
struct Picture {const uint16_t *pixels;uint64_t ticket;uint16_t width,height;uint8_t index;};
int main() {
  static std::array<uint16_t,90000> a{},b{};
  lv_init();auto *display=lv_display_create(400,400);
  auto *parent=lv_obj_create(lv_screen_active());
  lv_obj_set_size(parent,300,300);lv_obj_set_style_pad_all(parent,0,0);
  lv_obj_set_style_border_width(parent,1,0);
  auto *label=lv_label_create(parent);lv_label_set_text(label,"PLACEHOLDER");
  usp_spotify_artwork_lcd::Image image;
  Picture first{a.data(),1,300,300,0};Picture next{b.data(),2,150,300,1};
  assert(!image.apply(label,&first));assert(image.prepare(parent));
  assert(image.apply(label,&first) && image.visible());
  assert(!lv_obj_has_flag(image.object(),LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(image.object(),LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_update_layout(parent);
  assert(lv_obj_get_x(image.object())==0 && lv_obj_get_style_border_width(parent,LV_PART_MAIN)==0);
  const auto *old=static_cast<const lv_image_dsc_t *>(lv_image_get_src(image.object()));
  assert(old && old->data==reinterpret_cast<const uint8_t *>(a.data()) && old->data_size==180000);
  assert(lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN));
  assert(image.apply(label,&next));lv_obj_update_layout(parent);
  assert(lv_image_get_src(image.object())!=old && lv_obj_get_x(image.object())==75);
  assert(image.apply(label,static_cast<const Picture *>(nullptr)) && !image.visible());
  assert(!lv_image_get_src(image.object()) && !lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_get_style_border_width(parent,LV_PART_MAIN)==1);
  first.height=150;assert(image.apply(label,&first));lv_obj_update_layout(parent);
  assert(lv_image_get_src(image.object())==old && lv_obj_get_height(image.object())==150);
  first.width=301;assert(!image.apply(label,&first) && image.visible());
  assert(image.apply(label,static_cast<const Picture *>(nullptr)));
  lv_display_delete(display);lv_deinit();
  puts("ARTWORK_LCD_NATIVE=PASS actual_LVGL RGB565 descriptor_reuse aspect_center detach_header_cache no_device");
}
