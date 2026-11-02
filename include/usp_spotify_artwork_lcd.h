#pragma once
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <array>
#include <cstdint>
namespace usp_spotify_artwork_lcd {
static_assert(LV_USE_OS == LV_OS_NONE && LV_COLOR_DEPTH == 16,
              "Source loans require the checked single-main LVGL configuration");

/** Main-task-only image and stable descriptors. Never pass this to the worker.
 * Call prepare after LCD setup, before starting a cover transaction. Apply runs
 * between LVGL refresh calls; changing source and dropping its cache completes
 * before Cache acknowledges any old pixel loan. Keep this object alive with LCD.
 */
class Image {
 public:
  Image() = default;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  bool prepare(lv_obj_t *parent) {
    if (image_) return parent == parent_;
    if (!parent) return false;
    parent_ = parent;
    original_border_ = lv_obj_get_style_border_width(parent,LV_PART_MAIN);
    image_ = lv_image_create(parent);
    if (!image_) return false;
    lv_obj_add_flag(image_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(image_,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(image_,LV_OBJ_FLAG_SCROLLABLE);
    return true;
  }
  /** A failed application keeps loans retained by the caller for a later retry. */
  template<class Picture> bool apply(lv_obj_t *label,const Picture *picture) {
    if (!image_ || !label) return false;
    if (!picture) return clear_(label);
    if (picture->index >= 2 || !picture->pixels || !picture->width ||
        !picture->height || picture->width > 300 || picture->height > 300) return false;
    auto &descriptor = descriptors_[picture->index];
    // Drop this owned address before reusing a descriptor with new dimensions.
    drop_(&descriptor);
    descriptor = {};
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.w = picture->width;descriptor.header.h = picture->height;
    descriptor.header.stride = uint32_t(picture->width) * 2;
    descriptor.data_size = uint32_t(picture->width) * picture->height * 2;
    descriptor.data = reinterpret_cast<const uint8_t *>(picture->pixels);
    const void *old = lv_image_get_src(image_);
    ::lv_image_set_src(image_,&descriptor);
    if (lv_image_get_src(image_) != &descriptor) {
      lv_obj_add_flag(image_,LV_OBJ_FLAG_HIDDEN);visible_ = false;
      lv_obj_clear_flag(label,LV_OBJ_FLAG_HIDDEN);border_(original_border_);
      drop_(old);return false;
    }
    border_(0);
    lv_obj_align(image_,LV_ALIGN_CENTER,0,0);
    lv_obj_clear_flag(image_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);visible_ = true;
    if (old != &descriptor) drop_(old);
    return true;
  }
  bool visible() const { return visible_; }
  lv_obj_t *object() const { return image_; }
 private:
  void border_(int32_t width) {
    if (lv_obj_get_style_border_width(parent_,LV_PART_MAIN) != width)
      lv_obj_set_style_border_width(parent_,width,LV_PART_MAIN);
  }
  static void drop_(const void *source) { if (source) lv_image_cache_drop(source); }
  bool clear_(lv_obj_t *label) {
    const void *old = lv_image_get_src(image_);
    lv_obj_add_flag(image_,LV_OBJ_FLAG_HIDDEN);
    ::lv_image_set_src(image_,nullptr);
    if (lv_image_get_src(image_) != nullptr) return false;
    drop_(old);
    for (const auto &descriptor : descriptors_) drop_(&descriptor);
    lv_obj_clear_flag(label,LV_OBJ_FLAG_HIDDEN);border_(original_border_);visible_ = false;
    return true;
  }
  std::array<lv_image_dsc_t,2> descriptors_{};
  lv_obj_t *image_{nullptr},*parent_{nullptr};
  int32_t original_border_{0};bool visible_{false};
};
static_assert(sizeof(Image) <= 160, "Descriptors must remain small and stable");
/** One main-task controller keeps descriptor addresses stable for LCD lifetime. */
inline Image &controller() { static Image instance;return instance; }
}  // namespace usp_spotify_artwork_lcd
