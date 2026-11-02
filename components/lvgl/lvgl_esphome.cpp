#ifndef USE_HOST
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "lvgl_esphome.h"
#include "lvgl_native_copy_policy.h"
#include "esphome/components/mipi_rgb/mipi_rgb.h"
#include "esp_heap_caps.h"

#ifdef USE_ESP32
#include "esp_async_memcpy.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"
#endif

#include "core/lv_global.h"
#include "core/lv_obj_class_private.h"
#include "display/lv_display_private.h"
#include "draw/lv_draw_buf_private.h"

#include <cstring>
#include <numeric>

static void *lv_alloc_draw_buf(size_t size, bool internal);
static void *draw_buf_alloc_cb(size_t size, lv_color_format_t color_format) { return lv_alloc_draw_buf(size, false); };

namespace esphome::lvgl {
static const char *const TAG = "lvgl";

#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
constexpr size_t NATIVE_COPY_DMA_THRESHOLD = 64U * 1024U;
constexpr TickType_t NATIVE_COPY_DMA_WARN_TICKS = pdMS_TO_TICKS(250);

struct NativeCopyDmaRuntime {
  lv_draw_buf_handlers_t handlers{};
  lv_draw_buf_copy_cb_t fallback_copy{};
  async_memcpy_handle_t handle{};
  StaticSemaphore_t done_storage{};
  SemaphoreHandle_t done{};
  const void *buffer_1{};
  const void *buffer_2{};
  uint32_t width{};
  uint32_t height{};
  bool ready{};
};

static NativeCopyDmaRuntime native_copy_dma;

static bool IRAM_ATTR native_copy_dma_done(
    async_memcpy_handle_t, async_memcpy_event_t *, void *arg) {
  auto done = static_cast<SemaphoreHandle_t>(arg);
  BaseType_t high_task_wakeup = pdFALSE;
  xSemaphoreGiveFromISR(done, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}

static bool native_copy_pair(const lv_draw_buf_t *dest,
                             const lv_draw_buf_t *src) {
  const bool forward = dest->data == native_copy_dma.buffer_1 &&
                       src->data == native_copy_dma.buffer_2;
  const bool reverse = dest->data == native_copy_dma.buffer_2 &&
                       src->data == native_copy_dma.buffer_1;
  return forward || reverse;
}

static bool native_copy_same_area(const lv_area_t *left,
                                  const lv_area_t *right) {
  return left != nullptr && right != nullptr && left->x1 == right->x1 &&
         left->y1 == right->y1 && left->x2 == right->x2 &&
         left->y2 == right->y2;
}

static bool native_copy_dma_bytes(void *dest, const void *src, size_t bytes) {
  if (!native_copy_dma.ready || bytes == 0) return false;
  xSemaphoreTake(native_copy_dma.done, 0);
  const uint32_t started_us = micros();
  const esp_err_t err = esp_async_memcpy(
      native_copy_dma.handle, dest, const_cast<void *>(src), bytes,
      native_copy_dma_done, native_copy_dma.done);
  if (err != ESP_OK) return false;
  if (xSemaphoreTake(native_copy_dma.done, NATIVE_COPY_DMA_WARN_TICKS) != pdTRUE) {
    ESP_LOGW("lvgl.copy", "Native framebuffer DMA exceeded 250 ms bytes=%u",
             static_cast<unsigned>(bytes));
    xSemaphoreTake(native_copy_dma.done, portMAX_DELAY);
  }
  ESP_LOGD("lvgl.copy", "Native framebuffer DMA bytes=%u copy_us=%u",
           static_cast<unsigned>(bytes),
           static_cast<unsigned>(micros() - started_us));
  return true;
}

static void native_draw_buf_copy(lv_draw_buf_t *dest,
                                 const lv_area_t *dest_area,
                                 const lv_draw_buf_t *src,
                                 const lv_area_t *src_area) {
  auto fallback = native_copy_dma.fallback_copy;
  if (fallback == nullptr) return;
  if (!native_copy_dma.ready || !native_copy_pair(dest, src) ||
      !native_copy_same_area(dest_area, src_area) ||
      dest->header.cf != LV_COLOR_FORMAT_RGB565 ||
      src->header.cf != LV_COLOR_FORMAT_RGB565) {
    fallback(dest, dest_area, src, src_area);
    return;
  }
  const NativeCopyRegion area{
      dest_area->x1, dest_area->y1, dest_area->x2, dest_area->y2};
  const auto plan = native_copy_plan(
      area, native_copy_dma.width, native_copy_dma.height,
      dest->header.stride, sizeof(uint16_t), NATIVE_COPY_DMA_THRESHOLD);
  if (!plan.use_dma || src->header.stride != dest->header.stride ||
      !native_copy_dma_bytes(dest->data + plan.offset_bytes,
                             src->data + plan.offset_bytes,
                             plan.copy_bytes)) {
    fallback(dest, dest_area, src, src_area);
  }
}

static bool install_native_copy_dma(lv_display_t *display, void *buffer_1,
                                    void *buffer_2, uint32_t width,
                                    uint32_t height) {
  if (display == nullptr || display->buf_1 == nullptr ||
      display->buf_2 == nullptr || display->buf_1->handlers == nullptr ||
      display->buf_2->handlers != display->buf_1->handlers) return false;
  native_copy_dma.done = xSemaphoreCreateBinaryStatic(
      &native_copy_dma.done_storage);
  if (native_copy_dma.done == nullptr) return false;
  async_memcpy_config_t config = ASYNC_MEMCPY_DEFAULT_CONFIG();
  config.backlog = 1;
  config.dma_burst_size = 32;
  if (esp_async_memcpy_install_gdma_ahb(
          &config, &native_copy_dma.handle) != ESP_OK) return false;
  native_copy_dma.handlers = *display->buf_1->handlers;
  native_copy_dma.fallback_copy = native_copy_dma.handlers.buf_copy_cb;
  native_copy_dma.handlers.buf_copy_cb = native_draw_buf_copy;
  native_copy_dma.buffer_1 = buffer_1;
  native_copy_dma.buffer_2 = buffer_2;
  native_copy_dma.width = width;
  native_copy_dma.height = height;
  native_copy_dma.ready = native_copy_dma.fallback_copy != nullptr &&
                          display->buf_1->data == buffer_1 &&
                          display->buf_2->data == buffer_2;
  if (!native_copy_dma.ready) {
    esp_async_memcpy_uninstall(native_copy_dma.handle);
    native_copy_dma.handle = nullptr;
    return false;
  }
  display->buf_1->handlers = &native_copy_dma.handlers;
  display->buf_2->handlers = &native_copy_dma.handlers;
  return true;
}

struct QaFrameDmaWaitContext {
  SemaphoreHandle_t done{};
};

static bool IRAM_ATTR qa_frame_dma_done(async_memcpy_handle_t, async_memcpy_event_t *, void *arg) {
  auto *ctx = static_cast<QaFrameDmaWaitContext *>(arg);
  BaseType_t high_task_wakeup = pdFALSE;
  xSemaphoreGiveFromISR(ctx->done, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}
#endif

static const size_t MIN_BUFFER_FRAC = 8;     // buffer must be at least 1/8 of the display size
static const size_t MIN_BUFFER_SIZE = 2048;  // Sensible minimum buffer size

static const char *const EVENT_NAMES[] = {
    "NONE",
    "PRESSED",
    "PRESSING",
    "PRESS_LOST",
    "SHORT_CLICKED",
    "LONG_PRESSED",
    "LONG_PRESSED_REPEAT",
    "CLICKED",
    "RELEASED",
    "SCROLL_BEGIN",
    "SCROLL_END",
    "SCROLL",
    "GESTURE",
    "KEY",
    "FOCUSED",
    "DEFOCUSED",
    "LEAVE",
    "HIT_TEST",
    "COVER_CHECK",
    "REFR_EXT_DRAW_SIZE",
    "DRAW_MAIN_BEGIN",
    "DRAW_MAIN",
    "DRAW_MAIN_END",
    "DRAW_POST_BEGIN",
    "DRAW_POST",
    "DRAW_POST_END",
    "DRAW_PART_BEGIN",
    "DRAW_PART_END",
    "VALUE_CHANGED",
    "INSERT",
    "REFRESH",
    "READY",
    "CANCEL",
    "DELETE",
    "CHILD_CHANGED",
    "CHILD_CREATED",
    "CHILD_DELETED",
    "SCREEN_UNLOAD_START",
    "SCREEN_LOAD_START",
    "SCREEN_LOADED",
    "SCREEN_UNLOADED",
    "SIZE_CHANGED",
    "STYLE_CHANGED",
    "LAYOUT_CHANGED",
    "GET_SELF_SIZE",
};

static const unsigned LOG_LEVEL_MAP[] = {
    ESPHOME_LOG_LEVEL_DEBUG, ESPHOME_LOG_LEVEL_INFO,  ESPHOME_LOG_LEVEL_WARN,
    ESPHOME_LOG_LEVEL_ERROR, ESPHOME_LOG_LEVEL_ERROR, ESPHOME_LOG_LEVEL_NONE,

};

std::string lv_event_code_name_for(lv_event_t *event) {
  auto event_code = lv_event_get_code(event);
  if (event_code < sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0])) {
    return EVENT_NAMES[event_code];
  }
  // max 4 bytes: "%u" with uint8_t (max 255, 3 digits) + null
  char buf[4];
  snprintf(buf, sizeof(buf), "%u", event_code);
  return buf;
}

void LvglComponent::set_rotation(display::DisplayRotation rotation) {
  if (this->rotation_type_ == RotationType::ROTATION_UNUSED) {
    ESP_LOGW(TAG, "Display rotation cannot be changed unless rotation was enabled during setup.");
    return;
  }
  this->rotation_ = rotation;
  if (this->is_ready()) {
    this->set_resolution_();
    this->update_orientation_();
    lv_obj_update_layout(this->get_screen_active());
    lv_obj_invalidate(this->get_screen_active());
  }
}

void LvglComponent::set_rotation(int angle) {
  // Normalize to [0, 360). The DisplayRotation enum values are the angles in degrees.
  angle %= 360;
  if (angle < 0)
    angle += 360;
  if (angle % 90 != 0) {
    ESP_LOGW(TAG, "Invalid rotation angle %d; must be a multiple of 90 degrees.", angle);
    return;
  }
  this->set_rotation(static_cast<display::DisplayRotation>(angle));
}

void LvglComponent::rotate_coordinates(int32_t &x, int32_t &y) const {
  switch (this->rotation_) {
    default:
      break;

    case display::DISPLAY_ROTATION_180_DEGREES: {
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    }
    case display::DISPLAY_ROTATION_270_DEGREES: {
      auto tmp = x;
      x = this->height_ - y - 1;
      y = tmp;
      break;
    }
    case display::DISPLAY_ROTATION_90_DEGREES: {
      auto tmp = y;
      y = this->width_ - x - 1;
      x = tmp;
      break;
    }
  }
}

static void rounder_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  auto *area = static_cast<lv_area_t *>(lv_event_get_param(event));
  // cater for display driver chips with special requirements for bounds of partial
  // draw areas. Extend the draw area to satisfy:
  // * Coordinates must be a multiple of draw_rounding
  auto draw_rounding = comp->draw_rounding;
  // round down the start coordinates
  area->x1 = area->x1 / draw_rounding * draw_rounding;
  area->y1 = area->y1 / draw_rounding * draw_rounding;
  // round up the end coordinates
  area->x2 = (area->x2 + draw_rounding) / draw_rounding * draw_rounding - 1;
  area->y2 = (area->y2 + draw_rounding) / draw_rounding * draw_rounding - 1;
}

void LvglComponent::render_end_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->draw_end_();
}

void LvglComponent::render_start_cb(lv_event_t *event) {
  ESP_LOGVV(TAG, "Draw start");
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->draw_start_();
}

void LvglComponent::profile_render_end_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->refresh_profile_.ready(millis());
  comp->home_cache_render_ready_();
}

void LvglComponent::profile_render_start_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->refresh_profile_.render_start(millis());
  comp->home_cache_render_started_();
}

void LvglComponent::profile_refresh_start_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->refresh_profile_.refresh_start(millis());
}

void LvglComponent::profile_layout_ready_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->refresh_profile_.layout_ready(millis());
}

lv_event_code_t lv_update_event;  // NOLINT
void LvglComponent::dump_config() {
  const size_t buffer_percent =
      this->buffer_frac_ > 0 ? 100 / this->buffer_frac_ : 0;
  ESP_LOGCONFIG(TAG,
                "LVGL:\n"
                "  Display width/height: %d x %d\n"
                "  Buffer size: %zu%%\n"
                "  Rotation: %d\n"
                "  Draw rounding: %d",
                this->width_, this->height_, buffer_percent, this->rotation_, (int) this->draw_rounding);
  if (this->rotation_type_ != ROTATION_UNUSED) {
    const char *rot_type = "hardware via display driver";
    if (this->rotation_type_ == RotationType::ROTATION_SOFTWARE) {
#ifdef USE_ESP32_VARIANT_ESP32P4
      rot_type = this->ppa_client_ != nullptr ? "software (PPA accelerated)" : "software";
#else
      rot_type = "software";
#endif
    }
    ESP_LOGCONFIG(TAG, "  Rotation type: %s", rot_type);
  }
}

void LvglComponent::set_paused(bool paused, bool show_snow) {
  this->paused_ = paused;
  this->show_snow_ = show_snow;
  if (!paused && lv_screen_active() != nullptr) {
    lv_display_trigger_activity(this->disp_);  // resets the inactivity time
    lv_obj_invalidate(lv_screen_active());
  }
  if (paused && this->pause_callback_ != nullptr)
    this->pause_callback_->trigger();
  if (!paused && this->resume_callback_ != nullptr)
    this->resume_callback_->trigger();
}

void LvglComponent::esphome_lvgl_init() {
  lv_init();
  // override draw buf alloc to ensure proper alignment for PPA
  LV_GLOBAL_DEFAULT()->draw_buf_handlers.buf_malloc_cb = draw_buf_alloc_cb;
  LV_GLOBAL_DEFAULT()->draw_buf_handlers.buf_free_cb = lv_free_core;
  LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers.buf_malloc_cb = draw_buf_alloc_cb;
  LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers.buf_free_cb = lv_free_core;
  LV_GLOBAL_DEFAULT()->font_draw_buf_handlers.buf_malloc_cb = draw_buf_alloc_cb;
  LV_GLOBAL_DEFAULT()->font_draw_buf_handlers.buf_free_cb = lv_free_core;
  lv_tick_set_cb([] { return millis(); });
  lv_update_event = static_cast<lv_event_code_t>(lv_event_register_id());
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event, void *user_data) {
  lv_obj_add_event_cb(obj, callback, event, user_data);
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1,
                                 lv_event_code_t event2, void *user_data) {
  add_event_cb(obj, callback, event1, user_data);
  add_event_cb(obj, callback, event2, user_data);
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1,
                                 lv_event_code_t event2, lv_event_code_t event3, void *user_data) {
  add_event_cb(obj, callback, event1, user_data);
  add_event_cb(obj, callback, event2, user_data);
  add_event_cb(obj, callback, event3, user_data);
}

void LvglComponent::add_page(LvPageType *page) {
  this->pages_.push_back(page);
  page->set_parent(this);
  lv_display_set_default(this->disp_);
  page->setup(this->pages_.size() - 1);
}

void LvglComponent::show_page(size_t index, lv_screen_load_anim_t anim, uint32_t time) {
  if (index >= this->pages_.size())
    return;
  this->current_page_ = index;
  if (anim == LV_SCREEN_LOAD_ANIM_NONE) {
    lv_screen_load(this->pages_[this->current_page_]->obj);
  } else {
    lv_screen_load_anim(this->pages_[this->current_page_]->obj, anim, time, 0, false);
  }
}

void LvglComponent::show_next_page(lv_screen_load_anim_t anim, uint32_t time) {
  if (this->pages_.empty() || (this->current_page_ == this->pages_.size() - 1 && !this->page_wrap_))
    return;
  size_t start = this->current_page_;
  do {
    this->current_page_ = (this->current_page_ + 1) % this->pages_.size();
    if (this->current_page_ == start)
      return;  // all pages have skip=true (guaranteed not to happen by YAML validation)
  } while (this->pages_[this->current_page_]->skip);  // skip empty pages()
  this->show_page(this->current_page_, anim, time);
}

void LvglComponent::show_prev_page(lv_screen_load_anim_t anim, uint32_t time) {
  if (this->pages_.empty() || (this->current_page_ == 0 && !this->page_wrap_))
    return;
  size_t start = this->current_page_;
  do {
    this->current_page_ = (this->current_page_ + this->pages_.size() - 1) % this->pages_.size();
    if (this->current_page_ == start)
      return;  // all pages have skip=true (guaranteed not to happen by YAML validation)
  } while (this->pages_[this->current_page_]->skip);  // skip empty pages()
  this->show_page(this->current_page_, anim, time);
}

size_t LvglComponent::get_current_page() const { return this->current_page_; }
bool LvPageType::is_showing() const { return this->parent_->get_current_page() == this->index; }

#ifdef USE_ESP32_VARIANT_ESP32P4
bool LvglComponent::ppa_rotate_(const lv_color_data *src, lv_color_data *dst, uint16_t width, uint16_t height,
                                uint32_t height_rounded) {
  ppa_srm_rotation_angle_t angle;
  uint16_t out_w, out_h;

  // Map ESPHome clockwise display rotation to PPA counter-clockwise angles
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      angle = PPA_SRM_ROTATION_ANGLE_270;  // 270° CCW = 90° CW
      out_w = height_rounded;
      out_h = width;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      angle = PPA_SRM_ROTATION_ANGLE_180;
      out_w = width;
      out_h = height;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      angle = PPA_SRM_ROTATION_ANGLE_90;  // 90° CCW = 270° CW
      out_w = height_rounded;
      out_h = width;
      break;
    default:
      return false;  // No rotation needed
  }

  // Align buffer size to cache line (LV_DRAW_BUF_ALIGN) as required by PPA DMA
  // the underlying buffer will be large enough as the size is also padded when allocating.
  size_t out_buf_size = out_w * out_h * sizeof(lv_color_data);
  out_buf_size = LV_ROUND_UP(out_buf_size, LV_DRAW_BUF_ALIGN);

  ppa_srm_oper_config_t srm_config{};
  srm_config.in.buffer = src;
  srm_config.in.pic_w = width;
  srm_config.in.pic_h = height;
  srm_config.in.block_w = width;
  srm_config.in.block_h = height;
#if LV_COLOR_DEPTH == 16
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
#elif LV_COLOR_DEPTH == 32
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
#endif
  srm_config.out.buffer = dst;
  srm_config.out.buffer_size = out_buf_size;
  srm_config.out.pic_w = out_w;
  srm_config.out.pic_h = out_h;
#if LV_COLOR_DEPTH == 16
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
#elif LV_COLOR_DEPTH == 32
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
#endif
  srm_config.rotation_angle = angle;
  srm_config.scale_x = 1.0f;
  srm_config.scale_y = 1.0f;
  srm_config.mode = PPA_TRANS_MODE_BLOCKING;

  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_client_, &srm_config);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "PPA rotation failed: %s", esp_err_to_name(ret));
    ESP_LOGW(TAG, "PPA SRM: in=%ux%u src=%p, out=%ux%u dst=%p size=%zu, angle=%d", width, height, src, out_w, out_h,
             dst, out_buf_size, (int) angle);
    return false;
  }
  return true;
}
#endif  // USE_ESP32_VARIANT_ESP32P4

void LvglComponent::draw_buffer_(const lv_area_t *area, lv_color_data *ptr) {
  auto width = lv_area_get_width(area);
  auto height = lv_area_get_height(area);
  auto height_rounded = (height + this->draw_rounding - 1) / this->draw_rounding * this->draw_rounding;
  auto x1 = area->x1;
  auto y1 = area->y1;
  if (this->rotation_type_ == ROTATION_SOFTWARE) {
    lv_color_data *dst = reinterpret_cast<lv_color_data *>(this->rotate_buf_);
#ifdef USE_ESP32_VARIANT_ESP32P4
    bool ppa_done = this->ppa_client_ != nullptr && this->ppa_rotate_(ptr, dst, width, height, height_rounded);
    if (!ppa_done)
#endif
    {
      switch (this->rotation_) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          for (lv_coord_t x = height; x-- != 0;) {
            for (lv_coord_t y = 0; y != width; y++) {
              dst[y * height_rounded + x] = *ptr++;
            }
          }
          break;

        case display::DISPLAY_ROTATION_180_DEGREES:
          for (lv_coord_t y = height; y-- != 0;) {
            for (lv_coord_t x = width; x-- != 0;) {
              dst[y * width + x] = *ptr++;
            }
          }
          break;

        case display::DISPLAY_ROTATION_270_DEGREES:
          for (lv_coord_t x = 0; x != height; x++) {
            for (lv_coord_t y = width; y-- != 0;) {
              dst[y * height_rounded + x] = *ptr++;
            }
          }
          break;

        default:
          dst = ptr;
          break;
      }
    }
    // Coordinate adjustments apply regardless of PPA or SW rotation
    switch (this->rotation_) {
      case display::DISPLAY_ROTATION_90_DEGREES:
        y1 = x1;
        x1 = this->width_ - area->y1 - height;
        height = width;
        width = height_rounded;
        break;

      case display::DISPLAY_ROTATION_180_DEGREES:
        x1 = this->width_ - x1 - width;
        y1 = this->height_ - y1 - height;
        break;

      case display::DISPLAY_ROTATION_270_DEGREES:
        x1 = y1;
        y1 = this->height_ - area->x1 - width;
        height = width;
        width = height_rounded;
        break;

      default:
        break;
    }
    ptr = dst;
  }
  for (auto *display : this->displays_) {
    display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) ptr, display::COLOR_ORDER_RGB, LV_BITNESS,
                            this->big_endian_);
  }
}

void LvglComponent::flush_cb_(
    lv_display_t *disp_drv,
    const lv_area_t *area,
    uint8_t *color_p) {
  if (this->paused_) {
    lv_display_flush_ready(
        disp_drv);
    return;
  }

  // DIRECT mode can report several dirty areas.
  // Only the final one switches the native FB.
  if (
      !lv_display_flush_is_last(
          disp_drv)) {
    lv_display_flush_ready(
        disp_drv);
    return;
  }

  auto *rgb_display =
      static_cast<
          ::esphome::mipi_rgb::MipiRgb *>(
              this->displays_[0]);

  const int width =
      lv_display_get_horizontal_resolution(
          disp_drv);

  const int height =
      lv_display_get_vertical_resolution(
          disp_drv);

  auto now = millis();

  // Native DIRECT framebuffer is presented
  // as a complete frame, regardless of the
  // dirty rectangle LVGL just rendered.
  const bool submitted = rgb_display->present_native_frame(
      color_p,
      0,
      0,
      width,
      height);
  const uint32_t submit_elapsed_ms = millis() - now;
  static ProfileLogWindow startup_flush_submit_profile_seq;
  const bool log_submit = startup_flush_submit_profile_seq.observe(millis(), submit_elapsed_ms, 25, 6);
  const uint32_t submit_seq = startup_flush_submit_profile_seq.sequence;
  if (log_submit) {
    ESP_LOGI(TAG, "LVGL_FLUSH_SUBMIT_PROFILE seq=%u submit_ms=%u ok=%s area=%dx%d",
             static_cast<unsigned>(submit_seq),
             static_cast<unsigned>(submit_elapsed_ms), YESNO(submitted),
             static_cast<int>(lv_area_get_width(area)),
             static_cast<int>(lv_area_get_height(area)));
  }
  if (!submitted) {
    ESP_LOGW(
        TAG,
        "Native DIRECT submit failed");

    // Submission failed: no completion event
    // will arrive, so unblock LVGL here.
    lv_display_flush_ready(
        disp_drv);
    return;
  }

  this->last_presented_frame_ = color_p;
  this->last_presented_frame_complete_ = false;

  ESP_LOGV(
      TAG,
      "DIRECT submit after dirty "
      "%d/%d %d/%d took %dms",
      (int) area->x1,
      (int) area->y1,
      (int) lv_area_get_width(area),
      (int) lv_area_get_height(area),
      (int) (millis() - now));

  // IMPORTANT:
  // Do not call lv_display_flush_ready() here.
  // flush_wait_cb does that only after
  // on_frame_buf_complete.
}

bool LvglComponent::qa_capture_native_frame() {
  if (this->disp_ == nullptr || this->displays_.size() != 1 || this->paused_ || this->qa_frame_cache_hold_ || this->qa_native_resident_hold_) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_CAPTURE rejected display-not-ready");
    return false;
  }

  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2)) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_CAPTURE rejected native-buffers-unavailable");
    return false;
  }
  if (this->last_presented_frame_ != buf1 && this->last_presented_frame_ != buf2) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_CAPTURE rejected unknown-current-buffer");
    return false;
  }

  // LVGL DIRECT double buffering intentionally leaves the final flush pending
  // until the next redraw. Complete that exact pending flush here before the
  // QA probe copies either native framebuffer. Calling flush_ready after the
  // hardware completion is equivalent to what LVGL's next wait_for_flushing()
  // would do, and prevents a later callback from consuming the same event.
  const uint32_t sync_started_us = micros();
  bool frame_complete = this->last_presented_frame_complete_;
  if (!frame_complete) {
    frame_complete = rgb_display->wait_native_frame_complete();
    if (frame_complete) {
      this->last_presented_frame_complete_ = true;
      lv_display_flush_ready(this->disp_);
    }
  }
  const uint32_t sync_us = micros() - sync_started_us;
  if (!frame_complete) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_CAPTURE rejected pending-frame-timeout sync_us=%u",
             static_cast<unsigned>(sync_us));
    return false;
  }

  const size_t frame_bytes = static_cast<size_t>(this->get_width()) *
                             static_cast<size_t>(this->get_height()) * sizeof(uint16_t);
  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t free_before = heap_caps_get_free_size(caps);
  const size_t largest_before = heap_caps_get_largest_free_block(caps);
  const uint32_t alloc_started_us = micros();
  bool allocated_now = false;
  if (this->qa_frame_cache_ == nullptr) {
    this->qa_frame_cache_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, frame_bytes, caps));
    allocated_now = this->qa_frame_cache_ != nullptr;
  }
  const uint32_t alloc_us = micros() - alloc_started_us;
  if (this->qa_frame_cache_ == nullptr) {
    ESP_LOGW("usp.qa.frame",
             "FRAME_CACHE_CAPTURE allocation-failed bytes=%u free_before=%u largest_before=%u alloc_us=%u",
             static_cast<unsigned>(frame_bytes), static_cast<unsigned>(free_before),
             static_cast<unsigned>(largest_before), static_cast<unsigned>(alloc_us));
    return false;
  }

  this->qa_lvgl_onscreen_frame_ = this->last_presented_frame_;
  this->qa_lvgl_draw_frame_ = this->last_presented_frame_ == buf1 ? buf2 : buf1;
  this->qa_frame_cache_hold_ = true;
  const uint32_t copy_started_us = micros();
  std::memcpy(this->qa_frame_cache_, this->qa_lvgl_onscreen_frame_, frame_bytes);
  const uint32_t copy_us = micros() - copy_started_us;
  this->qa_frame_cache_bytes_ = frame_bytes;
  this->qa_frame_cache_valid_ = true;
  // Reuse the component's established pause path instead of changing normal
  // loop behavior. From this point until release, LVGL will not render/flush.
  this->set_paused(true, false);

  // Prepare the optional AHB-GDMA copy engine while Home is being cached, not
  // on the latency-critical navigation path. Backlog=1 keeps internal-memory
  // metadata small on the ESP32-S3, where ordinary heap headroom can be tight.
  uint32_t dma_install_us = 0;
  int dma_install_error = ESP_ERR_NOT_SUPPORTED;
  const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
  if (this->qa_dma_handle_ == nullptr) {
    async_memcpy_config_t dma_config = ASYNC_MEMCPY_DEFAULT_CONFIG();
    dma_config.backlog = 1;
    dma_config.dma_burst_size = 32;
    async_memcpy_handle_t dma_handle = nullptr;
    const uint32_t dma_install_started_us = micros();
    const esp_err_t err = esp_async_memcpy_install_gdma_ahb(&dma_config, &dma_handle);
    dma_install_us = micros() - dma_install_started_us;
    dma_install_error = static_cast<int>(err);
    if (err == ESP_OK) {
      this->qa_dma_handle_ = dma_handle;
    }
  } else {
    dma_install_error = ESP_OK;
  }
#endif
  const size_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t free_after = heap_caps_get_free_size(caps);
  const size_t largest_after = heap_caps_get_largest_free_block(caps);
  ESP_LOGI("usp.qa.frame",
           "FRAME_CACHE_CAPTURE bytes=%u source=%p draw=%p alloc_new=%s sync_us=%u alloc_us=%u copy_us=%u "
           "free_before=%u largest_before=%u free_after=%u largest_after=%u dma_ready=%s dma_install_us=%u "
           "dma_install_err=%d internal_before=%u internal_after=%u",
           static_cast<unsigned>(frame_bytes), this->qa_lvgl_onscreen_frame_, this->qa_lvgl_draw_frame_,
           YESNO(allocated_now), static_cast<unsigned>(sync_us), static_cast<unsigned>(alloc_us),
           static_cast<unsigned>(copy_us), static_cast<unsigned>(free_before),
           static_cast<unsigned>(largest_before), static_cast<unsigned>(free_after),
           static_cast<unsigned>(largest_after), YESNO(this->qa_dma_handle_ != nullptr),
           static_cast<unsigned>(dma_install_us), dma_install_error, static_cast<unsigned>(internal_before),
           static_cast<unsigned>(internal_after));
  return true;
}

bool LvglComponent::qa_present_cached_native_frame() {
  if (!this->qa_frame_cache_valid_ || this->qa_frame_cache_ == nullptr ||
      this->qa_frame_cache_bytes_ == 0 || !this->qa_frame_cache_hold_ ||
      !this->last_presented_frame_complete_ || this->disp_ == nullptr ||
      this->displays_.size() != 1 || (this->paused_ && !this->qa_frame_cache_hold_)) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_PRESENT rejected cache-or-display-not-ready");
    return false;
  }

  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2)) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_PRESENT rejected native-buffers-unavailable");
    return false;
  }
  void *target = nullptr;
  if (this->last_presented_frame_ == buf1) target = buf2;
  else if (this->last_presented_frame_ == buf2) target = buf1;
  if (target == nullptr) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_PRESENT rejected unknown-current-buffer");
    return false;
  }

  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t free_before = heap_caps_get_free_size(caps);
  const size_t largest_before = heap_caps_get_largest_free_block(caps);
  const uint32_t e2e_started_us = micros();
  const uint32_t copy_started_us = micros();
  std::memcpy(target, this->qa_frame_cache_, this->qa_frame_cache_bytes_);
  const uint32_t copy_us = micros() - copy_started_us;

  const uint32_t submit_started_us = micros();
  const bool submitted = rgb_display->present_native_frame(
      target, 0, 0, this->get_width(), this->get_height());
  const uint32_t submit_us = micros() - submit_started_us;
  if (!submitted) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_PRESENT submit-failed copy_us=%u submit_us=%u",
             static_cast<unsigned>(copy_us), static_cast<unsigned>(submit_us));
    return false;
  }

  this->last_presented_frame_ = target;
  this->last_presented_frame_complete_ = false;
  const uint32_t wait_started_us = micros();
  const bool complete = rgb_display->wait_native_frame_complete();
  const uint32_t wait_us = micros() - wait_started_us;
  const uint32_t e2e_us = micros() - e2e_started_us;
  this->last_presented_frame_complete_ = complete;
  const size_t free_after = heap_caps_get_free_size(caps);
  const size_t largest_after = heap_caps_get_largest_free_block(caps);
  ESP_LOGI("usp.qa.frame",
           "FRAME_CACHE_PRESENT bytes=%u target=%p copy_us=%u submit_us=%u wait_us=%u e2e_us=%u "
           "complete=%s free_before=%u largest_before=%u free_after=%u largest_after=%u",
           static_cast<unsigned>(this->qa_frame_cache_bytes_), target,
           static_cast<unsigned>(copy_us), static_cast<unsigned>(submit_us),
           static_cast<unsigned>(wait_us), static_cast<unsigned>(e2e_us), YESNO(complete),
           static_cast<unsigned>(free_before), static_cast<unsigned>(largest_before),
           static_cast<unsigned>(free_after), static_cast<unsigned>(largest_after));
  return complete;
}

bool LvglComponent::qa_present_cached_native_frame_dma() {
  if (!this->qa_frame_cache_valid_ || this->qa_frame_cache_ == nullptr ||
      this->qa_frame_cache_bytes_ == 0 || !this->qa_frame_cache_hold_ ||
      !this->last_presented_frame_complete_ || this->disp_ == nullptr ||
      this->displays_.size() != 1 || this->qa_dma_handle_ == nullptr) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT rejected cache-dma-or-display-not-ready");
    return false;
  }

#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2)) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT rejected native-buffers-unavailable");
    return false;
  }
  void *target = nullptr;
  if (this->last_presented_frame_ == buf1) target = buf2;
  else if (this->last_presented_frame_ == buf2) target = buf1;
  if (target == nullptr) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT rejected unknown-current-buffer");
    return false;
  }

  StaticSemaphore_t done_storage{};
  SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&done_storage);
  QaFrameDmaWaitContext wait_context{done};
  auto dma_handle = static_cast<async_memcpy_handle_t>(this->qa_dma_handle_);
  constexpr uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  constexpr uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t psram_before = heap_caps_get_free_size(psram_caps);
  const size_t internal_before = heap_caps_get_free_size(internal_caps);
  const uint32_t e2e_started_us = micros();
  const uint32_t dma_started_us = micros();
  const uint32_t dma_submit_started_us = micros();
  const esp_err_t dma_err = esp_async_memcpy(
      dma_handle, target, this->qa_frame_cache_, this->qa_frame_cache_bytes_, qa_frame_dma_done, &wait_context);
  const uint32_t dma_submit_us = micros() - dma_submit_started_us;
  if (dma_err != ESP_OK) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT submit-failed err=%d dma_submit_us=%u",
             static_cast<int>(dma_err), static_cast<unsigned>(dma_submit_us));
    return false;
  }
  const uint32_t dma_wait_started_us = micros();
  const bool dma_complete = xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE;
  const uint32_t dma_wait_us = micros() - dma_wait_started_us;
  const uint32_t dma_copy_us = micros() - dma_started_us;
  if (!dma_complete) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT timeout dma_submit_us=%u dma_wait_us=%u",
             static_cast<unsigned>(dma_submit_us), static_cast<unsigned>(dma_wait_us));
    // The DMA request was accepted, so its ISR callback still owns references
    // to this stack-backed semaphore/context. Drain that callback before the
    // function returns; this mirrors the normal framebuffer DMA path above.
    xSemaphoreTake(done, portMAX_DELAY);
    return false;
  }

  const uint32_t panel_submit_started_us = micros();
  const bool submitted = rgb_display->present_native_frame(
      target, 0, 0, this->get_width(), this->get_height());
  const uint32_t panel_submit_us = micros() - panel_submit_started_us;
  if (!submitted) {
    ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT panel-submit-failed dma_copy_us=%u panel_submit_us=%u",
             static_cast<unsigned>(dma_copy_us), static_cast<unsigned>(panel_submit_us));
    return false;
  }

  this->last_presented_frame_ = target;
  this->last_presented_frame_complete_ = false;
  const uint32_t wait_started_us = micros();
  const bool complete = rgb_display->wait_native_frame_complete();
  const uint32_t wait_us = micros() - wait_started_us;
  const uint32_t e2e_us = micros() - e2e_started_us;
  this->last_presented_frame_complete_ = complete;
  const size_t psram_after = heap_caps_get_free_size(psram_caps);
  const size_t internal_after = heap_caps_get_free_size(internal_caps);
  ESP_LOGI("usp.qa.frame",
           "FRAME_CACHE_DMA_PRESENT bytes=%u target=%p dma_submit_us=%u dma_wait_us=%u dma_copy_us=%u "
           "panel_submit_us=%u wait_us=%u e2e_us=%u complete=%s psram_before=%u psram_after=%u "
           "internal_before=%u internal_after=%u",
           static_cast<unsigned>(this->qa_frame_cache_bytes_), target, static_cast<unsigned>(dma_submit_us),
           static_cast<unsigned>(dma_wait_us), static_cast<unsigned>(dma_copy_us),
           static_cast<unsigned>(panel_submit_us), static_cast<unsigned>(wait_us), static_cast<unsigned>(e2e_us),
           YESNO(complete), static_cast<unsigned>(psram_before), static_cast<unsigned>(psram_after),
           static_cast<unsigned>(internal_before), static_cast<unsigned>(internal_after));
  return complete;
#else
  ESP_LOGW("usp.qa.frame", "FRAME_CACHE_DMA_PRESENT rejected unsupported-target");
  return false;
#endif
}

/** Bind stable Home and overlay objects without changing an existing binding. */
void LvglComponent::set_home_native_frame_context(lv_obj_t *root, lv_obj_t *menu,
                                                  lv_obj_t *alarm) {
  if (root == this->home_cache_root_ && menu == this->home_cache_menu_ &&
      alarm == this->home_cache_alarm_) return;
  this->home_cache_root_ = root;
  this->home_cache_menu_ = menu;
  this->home_cache_alarm_ = alarm;
  this->home_render_context_clean_ = false;
  this->home_render_pending_ = false;
  this->home_last_render_context_clean_ = false;
  this->home_native_frame_cache_valid_ = false;
}

/** Require known overlays; capture additionally requires the actual Home screen. */
bool LvglComponent::home_cache_context_clean_(bool capture) const {
  if (this->disp_ == nullptr || this->home_cache_root_ == nullptr ||
      this->home_cache_menu_ == nullptr || this->home_cache_alarm_ == nullptr)
    return false;
  if (!lv_obj_has_flag(this->home_cache_menu_, LV_OBJ_FLAG_HIDDEN) ||
      !lv_obj_has_flag(this->home_cache_alarm_, LV_OBJ_FLAG_HIDDEN)) return false;
  return !capture ||
         lv_display_get_screen_active(this->disp_) == this->home_cache_root_;
}

/** Remember the context that is actually being drawn, rather than a YAML page id. */
void LvglComponent::home_cache_render_started_() {
  this->home_render_pending_ = true;
  this->home_render_context_clean_ = this->home_cache_context_clean_(true);
}

/** A hidden overlay is safe only after a new clean Home frame has completed. */
void LvglComponent::home_cache_render_ready_() {
  if (!this->home_render_pending_) return;
  this->home_render_pending_ = false;
  this->home_last_render_context_clean_ = this->home_render_context_clean_ &&
                                          this->home_cache_context_clean_(true);
  this->home_render_context_clean_ = false;
}

/** Verify the copied source is an LVGL buffer and return the dedicated cache. */
void *LvglComponent::home_cache_native_buffer_(::esphome::mipi_rgb::MipiRgb *rgb_display) {
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  void *buf3 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2, &buf3) ||
      (this->last_presented_frame_ != buf1 && this->last_presented_frame_ != buf2))
    return nullptr;
  return buf3;
}

/** Consume the existing pending LVGL flush exactly once after DMA completion. */
bool LvglComponent::home_cache_wait_frame_(::esphome::mipi_rgb::MipiRgb *rgb_display) {
  if (this->last_presented_frame_complete_) return true;
  if (!rgb_display->wait_native_frame_complete()) return false;
  this->last_presented_frame_complete_ = true;
  lv_display_flush_ready(this->disp_);
  return true;
}

bool LvglComponent::refresh_home_native_frame_cache() {
  this->home_native_frame_cache_valid_ = false;
  if (!this->home_cache_context_clean_(true) ||
      !this->home_last_render_context_clean_ || this->displays_.size() != 1 ||
      this->paused_ || this->qa_frame_cache_hold_ || this->qa_native_resident_hold_) {
    ESP_LOGI("usp.home.fast", "HOME_FAST_CACHE_REFRESH rejected context-or-display");
    return false;
  }
  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf3 = this->home_cache_native_buffer_(rgb_display);
  if (buf3 == nullptr) return false;
  const uint32_t sync_started_us = micros();
  if (!this->home_cache_wait_frame_(rgb_display)) return false;
  const uint32_t sync_us = micros() - sync_started_us;
  const size_t frame_bytes = static_cast<size_t>(this->get_width()) *
                             static_cast<size_t>(this->get_height()) * sizeof(uint16_t);
  const uint32_t copy_started_us = micros();
  bool copied_by_dma = false;
#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
  copied_by_dma = native_copy_dma_bytes(
      buf3, this->last_presented_frame_, frame_bytes);
#endif
  if (!copied_by_dma) {
    std::memcpy(buf3, this->last_presented_frame_, frame_bytes);
  }
  const uint32_t copy_us = micros() - copy_started_us;
  this->home_native_frame_cache_ = buf3;
  this->home_native_frame_cache_bytes_ = frame_bytes;
  this->home_native_frame_cache_data_key_ = this->home_native_frame_data_key_;
  this->home_native_frame_cache_valid_ = true;
  this->home_native_frame_cache_generation_++;
  ESP_LOGI("usp.home.fast",
           "HOME_FAST_CACHE_REFRESH generation=%u bytes=%u source=%p resident=%p sync_us=%u copy_us=%u dma=%s",
           static_cast<unsigned>(this->home_native_frame_cache_generation_),
           static_cast<unsigned>(frame_bytes), this->last_presented_frame_, buf3,
           static_cast<unsigned>(sync_us), static_cast<unsigned>(copy_us),
           YESNO(copied_by_dma));
  return true;
}

bool LvglComponent::present_home_native_frame_cache() {
  if (!this->home_cache_context_clean_(false) ||
      !this->home_native_frame_cache_valid_ || this->home_native_frame_cache_ == nullptr ||
      this->home_native_frame_cache_bytes_ == 0 ||
      this->home_native_frame_cache_data_key_ != this->home_native_frame_data_key_ ||
      this->displays_.size() != 1 || this->paused_ || this->qa_frame_cache_hold_ ||
      this->qa_native_resident_hold_) {
    ESP_LOGI("usp.home.fast", "HOME_FAST_FIRST_PAINT cache=MISS");
    return false;
  }
  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  void *buf3 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2, &buf3) ||
      this->home_native_frame_cache_ != buf3) {
    this->home_native_frame_cache_valid_ = false;
    return false;
  }
  const uint32_t prior_started_us = micros();
  if (!this->home_cache_wait_frame_(rgb_display)) return false;
  return this->home_cache_present_buffer_(rgb_display, micros() - prior_started_us);
}

/** Keep native submission copy-free and preserve the existing DMA ownership. */
bool LvglComponent::home_cache_present_buffer_(::esphome::mipi_rgb::MipiRgb *rgb_display, uint32_t prior_wait_us) {
  const uint32_t started_us = micros();
  if (!rgb_display->present_native_frame(
      this->home_native_frame_cache_, 0, 0, this->get_width(), this->get_height()))
    return false;
  const uint32_t submit_us = micros() - started_us;
  this->last_presented_frame_ = this->home_native_frame_cache_;
  this->last_presented_frame_complete_ = false;
  const uint32_t wait_started_us = micros();
  const bool complete = rgb_display->wait_native_frame_complete();
  this->last_presented_frame_complete_ = complete;
  ESP_LOGI("usp.home.fast",
           "HOME_FAST_FIRST_PAINT cache=HIT generation=%u prior_wait_us=%u submit_us=%u wait_us=%u e2e_us=%u complete=%s",
           static_cast<unsigned>(this->home_native_frame_cache_generation_),
           static_cast<unsigned>(prior_wait_us), static_cast<unsigned>(submit_us),
           static_cast<unsigned>(micros() - wait_started_us),
           static_cast<unsigned>(micros() - started_us + prior_wait_us), YESNO(complete));
  return complete;
}

bool LvglComponent::qa_capture_native_resident_frame() {
  this->home_native_frame_cache_valid_ = false;
  if (this->disp_ == nullptr || this->displays_.size() != 1 || this->paused_ ||
      this->qa_frame_cache_hold_ || this->qa_native_resident_hold_) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_CAPTURE rejected display-not-ready");
    return false;
  }

  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  void *buf3 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2, &buf3)) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_CAPTURE rejected native-buffers-unavailable");
    return false;
  }
  if (this->last_presented_frame_ != buf1 && this->last_presented_frame_ != buf2) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_CAPTURE rejected unknown-lvgl-buffer");
    return false;
  }

  const uint32_t sync_started_us = micros();
  bool frame_complete = this->last_presented_frame_complete_;
  if (!frame_complete) {
    frame_complete = rgb_display->wait_native_frame_complete();
    if (frame_complete) {
      this->last_presented_frame_complete_ = true;
      lv_display_flush_ready(this->disp_);
    }
  }
  const uint32_t sync_us = micros() - sync_started_us;
  if (!frame_complete) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_CAPTURE rejected pending-frame-timeout sync_us=%u",
             static_cast<unsigned>(sync_us));
    return false;
  }

  const size_t frame_bytes = static_cast<size_t>(this->get_width()) *
                             static_cast<size_t>(this->get_height()) * sizeof(uint16_t);
  this->qa_native_resident_lvgl_frame_ = this->last_presented_frame_;
  this->qa_native_resident_frame_ = buf3;
  this->qa_native_resident_bytes_ = frame_bytes;
  this->qa_native_resident_hold_ = true;
  // Quiesce LVGL before the one-time cache refresh. The latency-critical
  // present path below never copies a full frame.
  this->set_paused(true, false);
  const uint32_t copy_started_us = micros();
  std::memcpy(this->qa_native_resident_frame_, this->qa_native_resident_lvgl_frame_, frame_bytes);
  const uint32_t copy_us = micros() - copy_started_us;
  constexpr uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  ESP_LOGI("usp.qa.frame",
           "FRAME_RESIDENT_CAPTURE bytes=%u source=%p resident=%p sync_us=%u copy_us=%u psram_free=%u "
           "psram_largest=%u",
           static_cast<unsigned>(frame_bytes), this->qa_native_resident_lvgl_frame_,
           this->qa_native_resident_frame_, static_cast<unsigned>(sync_us), static_cast<unsigned>(copy_us),
           static_cast<unsigned>(heap_caps_get_free_size(psram_caps)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(psram_caps)));
  return true;
}

bool LvglComponent::qa_present_native_resident_frame() {
  if (!this->qa_native_resident_hold_ || this->qa_native_resident_frame_ == nullptr ||
      this->qa_native_resident_bytes_ == 0 || !this->paused_ || !this->last_presented_frame_complete_ ||
      this->disp_ == nullptr || this->displays_.size() != 1) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_PRESENT rejected cache-or-display-not-ready");
    return false;
  }

  auto *rgb_display = static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0]);
  void *buf1 = nullptr;
  void *buf2 = nullptr;
  void *buf3 = nullptr;
  if (!rgb_display->get_native_frame_buffers(&buf1, &buf2, &buf3) ||
      this->qa_native_resident_frame_ != buf3) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_PRESENT rejected resident-buffer-mismatch");
    return false;
  }

  const uint32_t e2e_started_us = micros();
  const uint32_t submit_started_us = micros();
  const bool submitted = rgb_display->present_native_frame(
      this->qa_native_resident_frame_, 0, 0, this->get_width(), this->get_height());
  const uint32_t submit_us = micros() - submit_started_us;
  if (!submitted) {
    ESP_LOGW("usp.qa.frame", "FRAME_RESIDENT_PRESENT submit-failed submit_us=%u",
             static_cast<unsigned>(submit_us));
    return false;
  }

  this->last_presented_frame_ = this->qa_native_resident_frame_;
  this->last_presented_frame_complete_ = false;
  const uint32_t wait_started_us = micros();
  const bool complete = rgb_display->wait_native_frame_complete();
  const uint32_t wait_us = micros() - wait_started_us;
  const uint32_t e2e_us = micros() - e2e_started_us;
  this->last_presented_frame_complete_ = complete;
  ESP_LOGI("usp.qa.frame",
           "FRAME_RESIDENT_PRESENT bytes=%u target=%p submit_us=%u wait_us=%u e2e_us=%u complete=%s",
           static_cast<unsigned>(this->qa_native_resident_bytes_), this->qa_native_resident_frame_,
           static_cast<unsigned>(submit_us), static_cast<unsigned>(wait_us), static_cast<unsigned>(e2e_us),
           YESNO(complete));
  return complete;
}

void LvglComponent::qa_release_native_resident_frame() {
  auto *rgb_display = this->displays_.size() == 1
                          ? static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0])
                          : nullptr;
  bool restored = true;
  uint32_t restore_submit_us = 0;
  uint32_t restore_wait_us = 0;
  if (this->qa_native_resident_hold_ && rgb_display != nullptr &&
      this->qa_native_resident_lvgl_frame_ != nullptr) {
    if (!this->last_presented_frame_complete_) {
      restored = rgb_display->wait_native_frame_complete();
      this->last_presented_frame_complete_ = restored;
    }
    if (restored && this->last_presented_frame_ != this->qa_native_resident_lvgl_frame_) {
      const uint32_t submit_started_us = micros();
      restored = rgb_display->present_native_frame(
          this->qa_native_resident_lvgl_frame_, 0, 0, this->get_width(), this->get_height());
      restore_submit_us = micros() - submit_started_us;
      if (restored) {
        this->last_presented_frame_ = this->qa_native_resident_lvgl_frame_;
        this->last_presented_frame_complete_ = false;
        const uint32_t wait_started_us = micros();
        restored = rgb_display->wait_native_frame_complete();
        restore_wait_us = micros() - wait_started_us;
        this->last_presented_frame_complete_ = restored;
      }
    }
  }
  this->qa_native_resident_hold_ = false;
  this->qa_native_resident_frame_ = nullptr;
  this->qa_native_resident_lvgl_frame_ = nullptr;
  this->qa_native_resident_bytes_ = 0;
  this->set_paused(false, false);
  ESP_LOGI("usp.qa.frame", "FRAME_RESIDENT_RELEASED restored=%s restore_submit_us=%u restore_wait_us=%u",
           YESNO(restored), static_cast<unsigned>(restore_submit_us), static_cast<unsigned>(restore_wait_us));
}

void LvglComponent::qa_release_native_frame_cache() {
  auto *rgb_display = this->displays_.size() == 1
                          ? static_cast<::esphome::mipi_rgb::MipiRgb *>(this->displays_[0])
                          : nullptr;
  bool restored = true;
  uint32_t restore_copy_us = 0;
  uint32_t restore_wait_us = 0;

  // While the QA hold is active LVGL never renders into its off-screen buffer.
  // Before resuming, put the panel back on the framebuffer that LVGL believes
  // is on-screen. LVGL can then safely render into qa_lvgl_draw_frame_.
  if (this->qa_frame_cache_hold_ && rgb_display != nullptr && this->qa_lvgl_onscreen_frame_ != nullptr &&
      this->qa_frame_cache_valid_ && this->qa_frame_cache_ != nullptr) {
    if (!this->last_presented_frame_complete_) {
      restored = rgb_display->wait_native_frame_complete();
      this->last_presented_frame_complete_ = restored;
    }
    if (restored && this->last_presented_frame_ != this->qa_lvgl_onscreen_frame_) {
      const uint32_t copy_started_us = micros();
      std::memcpy(const_cast<void *>(this->qa_lvgl_onscreen_frame_), this->qa_frame_cache_,
                  this->qa_frame_cache_bytes_);
      restore_copy_us = micros() - copy_started_us;
      restored = rgb_display->present_native_frame(
          this->qa_lvgl_onscreen_frame_, 0, 0, this->get_width(), this->get_height());
      if (restored) {
        this->last_presented_frame_ = this->qa_lvgl_onscreen_frame_;
        this->last_presented_frame_complete_ = false;
        const uint32_t wait_started_us = micros();
        restored = rgb_display->wait_native_frame_complete();
        restore_wait_us = micros() - wait_started_us;
        this->last_presented_frame_complete_ = restored;
      }
    }
  }

#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
  int dma_uninstall_error = ESP_OK;
  if (this->qa_dma_handle_ != nullptr) {
    dma_uninstall_error = static_cast<int>(
        esp_async_memcpy_uninstall(static_cast<async_memcpy_handle_t>(this->qa_dma_handle_)));
    if (dma_uninstall_error == ESP_OK) this->qa_dma_handle_ = nullptr;
  }
#else
  int dma_uninstall_error = ESP_ERR_NOT_SUPPORTED;
#endif

  this->qa_frame_cache_hold_ = false;
  this->qa_lvgl_onscreen_frame_ = nullptr;
  this->qa_lvgl_draw_frame_ = nullptr;
  if (this->qa_frame_cache_ != nullptr) {
    heap_caps_free(this->qa_frame_cache_);
    this->qa_frame_cache_ = nullptr;
  }
  this->qa_frame_cache_bytes_ = 0;
  this->qa_frame_cache_valid_ = false;
  this->set_paused(false, false);
  ESP_LOGI("usp.qa.frame",
           "FRAME_CACHE_RELEASED restored=%s restore_copy_us=%u restore_wait_us=%u dma_uninstall_err=%d",
           YESNO(restored), static_cast<unsigned>(restore_copy_us), static_cast<unsigned>(restore_wait_us),
           dma_uninstall_error);
}

IdleTrigger::IdleTrigger(LvglComponent *parent, TemplatableFn<uint32_t> timeout) : timeout_(timeout) {
  parent->add_on_idle_callback([this](uint32_t idle_time) {
    if (!this->is_idle_ && idle_time > this->timeout_.value()) {
      this->is_idle_ = true;
      this->trigger();
    } else if (this->is_idle_ && idle_time < this->timeout_.value()) {
      this->is_idle_ = false;
    }
  });
}

#ifdef USE_LVGL_TOUCHSCREEN
LVTouchListener::LVTouchListener(uint16_t long_press_time, uint16_t long_press_repeat_time, LvglComponent *parent) {
  this->set_parent(parent);
  this->drv_ = lv_indev_create();
  lv_indev_set_type(this->drv_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_disp(this->drv_, parent->get_disp());
  lv_indev_set_long_press_time(this->drv_, long_press_time);
  lv_indev_set_long_press_repeat_time(this->drv_, long_press_repeat_time);
  lv_indev_set_user_data(this->drv_, this);
  lv_indev_set_read_cb(this->drv_, [](lv_indev_t *d, lv_indev_data_t *data) {
    auto *l = static_cast<LVTouchListener *>(lv_indev_get_user_data(d));
    if (l->touch_pressed_) {
      data->point.x = l->touch_point_.x;
      data->point.y = l->touch_point_.y;
      l->parent_->rotate_coordinates(data->point.x, data->point.y);
      data->state = LV_INDEV_STATE_PRESSED;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
    }
  });
}

void LVTouchListener::update(const touchscreen::TouchPoints_t &tpoints) {
  this->touch_pressed_ = !this->parent_->is_paused() && !tpoints.empty();
  if (this->touch_pressed_)
    this->touch_point_ = tpoints[0];
}
#endif  // USE_LVGL_TOUCHSCREEN

#ifdef USE_LVGL_METER

int16_t lv_get_needle_angle_for_value(lv_obj_t *obj, int32_t value) {
  auto *scale = lv_obj_get_parent(obj);
  auto min_value = lv_scale_get_range_min_value(scale);
  auto max_value = lv_scale_get_range_max_value(scale);
  value = clamp(value, min_value, max_value);
  return ((value - min_value) * lv_scale_get_angle_range(scale) / (max_value - min_value) +
          lv_scale_get_rotation((scale))) %
         360;
}

void IndicatorLine::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_line_set_points(lv_obj, this->points_, 2);
  lv_obj_add_event_cb(
      lv_obj_get_parent(obj),
      [](lv_event_t *e) {
        auto *indicator = static_cast<IndicatorLine *>(lv_event_get_user_data(e));
        indicator->update_length_();
        ESP_LOGV(TAG, "Updated length, value = %d", indicator->angle_);
      },
      LV_EVENT_SIZE_CHANGED, this);
}

void IndicatorLine::set_value(int value) {
  auto angle = lv_get_needle_angle_for_value(this->obj, value);
  if (angle != this->angle_) {
    this->angle_ = angle;
    this->update_length_();
  }
}

void IndicatorLine::update_length_() {
  auto cx = lv_obj_get_width(lv_obj_get_parent(this->obj)) / 2;
  auto cy = lv_obj_get_height(lv_obj_get_parent(this->obj)) / 2;
  auto radius = clamp_at_most(cx, cy);
  auto length = lv_obj_get_style_length(this->obj, LV_PART_MAIN);
  auto radial_offset = lv_obj_get_style_radial_offset(this->obj, LV_PART_MAIN);
  if (LV_COORD_IS_PCT(radial_offset)) {
    radial_offset = radius * LV_COORD_GET_PCT(radial_offset) / 100;
  }
  if (LV_COORD_IS_PCT(length)) {
    length = radius * LV_COORD_GET_PCT(length) / 100;
  } else if (length < 0) {
    length += radius;
  }
  auto x = lv_trigo_cos(this->angle_) / 32768.0f;
  auto y = lv_trigo_sin(this->angle_) / 32768.0f;
  // radius here also represents the offset of the scale center from top left
  this->points_[0].x = radius + radial_offset * x;
  this->points_[0].y = radius + radial_offset * y;
  this->points_[1].x = radius + x * (radial_offset + length);
  this->points_[1].y = radius + y * (radial_offset + length);
  lv_obj_refresh_self_size(this->obj);
  lv_obj_invalidate(this->obj);
}
#endif

#ifdef USE_LVGL_TABLE
uint32_t lv_table_get_selected_row(lv_obj_t *obj) {
  uint32_t row;
  uint32_t column;
  lv_table_get_selected_cell(obj, &row, &column);
  return row;
}

uint32_t lv_table_get_selected_column(lv_obj_t *obj) {
  uint32_t row;
  uint32_t column;
  lv_table_get_selected_cell(obj, &row, &column);
  return column;
}

void LvTableType::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_obj_add_event_cb(
      lv_obj,
      [](lv_event_t *e) {
        auto *table = static_cast<LvTableType *>(lv_event_get_user_data(e));
        table->update_column_widths_();
      },
      LV_EVENT_SIZE_CHANGED, this);
}

void LvTableType::add_column_width_pct(uint32_t col, uint8_t pct) {
  for (auto &i : this->column_pct_) {
    if (i.col == col) {
      i.pct = pct;
      this->update_column_widths_();
      return;
    }
  }
  this->column_pct_.push_back({col, pct});
  this->update_column_widths_();
}

void LvTableType::update_column_widths_() {
  auto content_width = lv_obj_get_content_width(this->obj);
  for (const auto &col : this->column_pct_) {
    lv_table_set_column_width(this->obj, col.col, content_width * col.pct / 100);
  }
}
#endif  // USE_LVGL_TABLE

#ifdef USE_LVGL_KEY_LISTENER
LVEncoderListener::LVEncoderListener(lv_indev_type_t type, uint16_t long_press_time, uint16_t long_press_repeat_time) {
  this->drv_ = lv_indev_create();
  lv_indev_set_type(this->drv_, type);
  lv_indev_set_long_press_time(this->drv_, long_press_time);
  lv_indev_set_long_press_repeat_time(this->drv_, long_press_repeat_time);
  lv_indev_set_user_data(this->drv_, this);
  lv_indev_set_read_cb(this->drv_, [](lv_indev_t *d, lv_indev_data_t *data) {
    auto *l = static_cast<LVEncoderListener *>(lv_indev_get_user_data(d));
    data->state = l->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->key = l->key_;
    data->enc_diff = (int16_t) (l->count_ - l->last_count_);
    l->last_count_ = l->count_;
    data->continue_reading = false;
  });
}
#endif  // USE_LVGL_KEY_LISTENER

#if defined(USE_LVGL_DROPDOWN) || defined(LV_USE_ROLLER)
std::string LvSelectable::get_selected_text() {
  auto selected = this->get_selected_index();
  if (selected >= this->options_.size())
    return "";
  return this->options_[selected];
}

static std::string join_string(const FixedVector<const char *> &options) {
  return std::accumulate(
      options.begin(), options.end(), std::string(),
      [](const std::string &a, const char *b) -> std::string { return a + (!a.empty() ? "\n" : "") + b; });
}

void LvSelectable::set_selected_text(const std::string &text, lv_anim_enable_t anim) {
  auto *index = std::find(this->options_.begin(), this->options_.end(), text);
  if (index != this->options_.end()) {
    this->set_selected_index(index - this->options_.begin(), anim);
    lv_obj_send_event(this->obj, lv_update_event, nullptr);
  }
}

void LvSelectable::set_options(FixedVector<const char *> options) {
  auto index = this->get_selected_index();
  if (index >= options.size())
    index = options.size() - 1;
  this->options_ = std::move(options);
  this->set_option_string(join_string(this->options_).c_str());
  lv_obj_send_event(this->obj, LV_EVENT_REFRESH, nullptr);
  this->set_selected_index(index, LV_ANIM_OFF);
}
#endif  // USE_LVGL_DROPDOWN || LV_USE_ROLLER

#ifdef USE_LVGL_BUTTONMATRIX
void LvButtonMatrixType::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_obj_add_event_cb(
      lv_obj,
      [](lv_event_t *event) {
        auto *self = static_cast<LvButtonMatrixType *>(lv_event_get_user_data(event));
        if (self->key_callback_.size() == 0)
          return;
        auto key_idx = lv_buttonmatrix_get_selected_button(self->obj);
        if (key_idx == LV_BUTTONMATRIX_BUTTON_NONE)
          return;
        if (self->key_map_.contains(key_idx)) {
          self->send_key_(self->key_map_[key_idx]);
          return;
        }
        const auto *str = lv_buttonmatrix_get_button_text(self->obj, key_idx);
        auto len = strlen(str);
        while (len--)
          self->send_key_(*str++);
      },
      LV_EVENT_PRESSED, this);
}
#endif  // USE_LVGL_BUTTONMATRIX

#ifdef USE_LVGL_KEYBOARD
static const char *const KB_SPECIAL_KEYS[] = {
    "abc", "ABC", "1#",
    // maybe add other special keys here
};

void LvKeyboardType::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_obj_add_event_cb(
      lv_obj,
      [](lv_event_t *event) {
        auto *self = static_cast<LvKeyboardType *>(lv_event_get_user_data(event));
        if (self->key_callback_.size() == 0)
          return;

        auto key_idx = lv_buttonmatrix_get_selected_button(self->obj);
        if (key_idx == LV_BUTTONMATRIX_BUTTON_NONE)
          return;
        const char *txt = lv_buttonmatrix_get_button_text(self->obj, key_idx);
        if (txt == nullptr)
          return;
        for (const auto *kb_special_key : KB_SPECIAL_KEYS) {
          if (strcmp(txt, kb_special_key) == 0)
            return;
        }
        while (*txt != 0)
          self->send_key_(*txt++);
      },
      LV_EVENT_PRESSED, this);
}
#endif  // USE_LVGL_KEYBOARD

void LvglComponent::draw_end_() {
  if (this->draw_end_callback_ != nullptr)
    this->draw_end_callback_->trigger();
  // Only reachable once the display is idle again: while busy, the display's refr_timer_ is
  // paused (see loop()), so LVGL never renders/flushes and this event never fires.
  if (this->update_when_display_idle_) {
    for (auto *disp : this->displays_)
      disp->update();
  }
}

bool LvglComponent::displays_busy_() const {
  if (!this->update_when_display_idle_)
    return false;
  for (auto *disp : this->displays_) {
    if (!disp->is_idle())
      return true;
  }
  return false;
}

void LvglComponent::write_random_() {
  int iterations = 6 - lv_display_get_inactive_time(this->disp_) / 60000;
  if (iterations <= 0)
    iterations = 1;
  int16_t width = lv_display_get_horizontal_resolution(this->disp_);
  int16_t height = lv_display_get_vertical_resolution(this->disp_);
  while (iterations-- != 0) {
    int32_t col = random_uint32() % width;
    col = col / this->draw_rounding * this->draw_rounding;
    int32_t row = random_uint32() % height;
    row = row / this->draw_rounding * this->draw_rounding;
    // size will be between 8 and 32, and a multiple of draw_rounding
    int32_t size = (random_uint32() % 25 + 8) / this->draw_rounding * this->draw_rounding;
    lv_area_t area{.x1 = col, .y1 = row, .x2 = col + size - 1, .y2 = row + size - 1};
    // clip to display bounds just in case
    if (area.x2 >= width)
      area.x2 = width - 1;
    if (area.y2 >= height)
      area.y2 = height - 1;

    // line_len can't exceed 1024, and minimum buffer size is 2048, so this won't overflow the buffer
    size_t line_len = lv_area_get_width(&area) * lv_area_get_height(&area) / 2;
    for (size_t i = 0; i != line_len; i++) {
      reinterpret_cast<uint32_t *>(this->draw_buf_)[i] = random_uint32();
    }
    this->draw_buffer_(&area, reinterpret_cast<lv_color_data *>(this->draw_buf_));
  }
}

/**
 * @class LvglComponent
 * @brief Component for rendering LVGL.
 *
 * This component renders LVGL widgets on a display. Some initialisation must be done in the constructor
 * since LVGL needs to be initialised before any widgets can be created.
 *
 * @param displays a list of displays to render onto. All displays must have the same
 *                 resolution.
 * @param buffer_frac the fraction of the display resolution to use for the LVGL
 *                    draw buffer. A higher value will make animations smoother but
 *                    also increase memory usage.
 * @param full_refresh if true, the display will be fully refreshed on every frame.
 *                     If false, only changed areas will be updated.
 * @param draw_rounding the rounding to use when drawing. A value of 1 will draw
 *                      without any rounding, a value of 2 will round to the nearest
 *                      multiple of 2, and so on.
 * @param resume_on_input if true, this component will resume rendering when the user
 *                         presses a key or clicks on the screen.
 * @param rotation_type What rotation type to use, if any
 */
LvglComponent::LvglComponent(std::vector<display::Display *> displays, float buffer_frac, bool full_refresh,
                             int draw_rounding, bool resume_on_input, bool update_when_display_idle,
                             RotationType rotation_type)
    : draw_rounding(draw_rounding),
      displays_(std::move(displays)),
      buffer_frac_(buffer_frac),
      full_refresh_(full_refresh),
      resume_on_input_(resume_on_input),
      update_when_display_idle_(update_when_display_idle),
      rotation_type_(rotation_type) {
  // The display dimensions are configured before any LVGL widgets are made.
  // Avoid resizing every populated screen during setup on the slower PSRAM.
  this->disp_ = lv_display_create(this->displays_[0]->get_native_width(),
                                  this->displays_[0]->get_native_height());
}

void LvglComponent::set_resolution_() const {
  int32_t width = this->width_;
  int32_t height = this->height_;
  if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES ||
      this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES) {
    std::swap(width, height);
  }
  ESP_LOGD(TAG, "Setting resolution to %u x %u (rotation %d)", (unsigned) width, (unsigned) height,
           (int) this->rotation_);
  if (this->rotation_type_ == RotationType::ROTATION_HARDWARE) {
    for (auto *display : this->displays_)
      display->set_rotation(this->rotation_);
  }
  lv_display_set_resolution(this->disp_, width, height);
}

void LvglComponent::update_orientation_() {
  // A square display is treated as landscape.
  auto orientation = this->get_width() >= this->get_height() ? Orientation::LANDSCAPE : Orientation::PORTRAIT;
  if (orientation == this->orientation_)
    return;
  this->orientation_ = orientation;
  auto *trigger = orientation == Orientation::LANDSCAPE ? this->landscape_callback_ : this->portrait_callback_;
  if (trigger != nullptr)
    trigger->trigger();
}

void LvglComponent::setup() {
  const uint32_t setup_started_ms = millis();
  ESP_LOGI(TAG, "LVGL_SETUP_PROFILE phase=start native=%dx%d",
           this->displays_[0]->get_native_width(),
           this->displays_[0]->get_native_height());
  auto *display = this->displays_[0];
  auto rounding = this->draw_rounding;
  this->width_ = display->get_native_width();
  this->height_ = display->get_native_height();
  // cater for displays with dimensions that don't divide by the required rounding
  auto width = (this->width_ + rounding - 1) / rounding * rounding;
  auto height = (this->height_ + rounding - 1) / rounding * rounding;
  if (
      width != this->width_ ||
      height != this->height_) {
    this->status_set_error(
        LOG_STR(
            "DIRECT requires native dimensions"));
    this->mark_failed();
    return;
  }

  auto *rgb_display =
      static_cast<
          ::esphome::mipi_rgb::MipiRgb *>(
              display);

  void *buf1 = nullptr;
  void *buf2 = nullptr;

  if (
      !rgb_display->get_native_frame_buffers(
          &buf1,
          &buf2)) {
    this->status_set_error(
        LOG_STR(
            "Native RGB buffers unavailable"));
    this->mark_failed();
    return;
  }

  const size_t buf_bytes =
      static_cast<size_t>(this->width_)
      * static_cast<size_t>(this->height_)
      * LV_COLOR_DEPTH
      / 8;

  this->buffer_frac_ = 1;
  this->draw_buf_ =
      static_cast<uint8_t *>(buf1);

  this->set_resolution_();

  lv_display_set_color_format(
      this->disp_,
      LV_COLOR_FORMAT_RGB565);

  lv_display_set_flush_cb(
      this->disp_,
      static_flush_cb);

  lv_display_set_user_data(
      this->disp_,
      this);

  lv_display_set_flush_wait_cb(
      this->disp_,
      [](lv_display_t *disp_drv) {
        auto *self =
            reinterpret_cast<LvglComponent *>(
                lv_display_get_user_data(
                    disp_drv));

        if (self == nullptr) {
          lv_display_flush_ready(
              disp_drv);
          return;
        }

        if (
            lv_display_flush_is_last(
                disp_drv)) {
          auto *rgb_display =
              static_cast<
                  ::esphome::mipi_rgb::MipiRgb *>(
                      self->displays_[0]);

          const uint32_t wait_started_ms = millis();
          const bool frame_complete =
              rgb_display->wait_native_frame_complete();
          self->last_presented_frame_complete_ = frame_complete;
          const uint32_t wait_elapsed_ms = millis() - wait_started_ms;
          static ProfileLogWindow startup_flush_wait_profile_seq;
          const bool log_wait = startup_flush_wait_profile_seq.observe(millis(), wait_elapsed_ms, 25, 6);
          const uint32_t wait_seq = startup_flush_wait_profile_seq.sequence;
          if (log_wait) {
            ESP_LOGI(TAG, "LVGL_FLUSH_WAIT_PROFILE seq=%u wait_ms=%u complete=%s",
                     static_cast<unsigned>(wait_seq),
                     static_cast<unsigned>(wait_elapsed_ms),
                     YESNO(frame_complete));
          }
          if (!frame_complete) {
            const bool recovered =
                rgb_display->recover_native_frame_sync();
            ESP_LOGW(
                TAG,
                "Native RGB frame-complete wait timed out; recovery=%s",
                YESNO(recovered));
          }
        }

        lv_display_flush_ready(
            disp_drv);
      });

  lv_display_add_event_cb(
      this->disp_,
      rounder_cb,
      LV_EVENT_INVALIDATE_AREA,
      this);

  lv_display_set_buffers(
      this->disp_,
      buf1,
      buf2,
      buf_bytes,
      LV_DISPLAY_RENDER_MODE_DIRECT);
#if defined(USE_ESP32) && SOC_AHB_GDMA_SUPPORTED && SOC_AHB_GDMA_SUPPORT_PSRAM
  const bool native_dma_ready = install_native_copy_dma(
      this->disp_, buf1, buf2, this->width_, this->height_);
  ESP_LOGI(TAG, "LVGL_NATIVE_COPY_DMA ready=%s threshold=%u",
           YESNO(native_dma_ready),
           static_cast<unsigned>(NATIVE_COPY_DMA_THRESHOLD));
#endif
  if (this->rotation_type_ == ROTATION_SOFTWARE) {
    this->rotate_buf_ = static_cast<lv_color_t *>(lv_alloc_draw_buf(buf_bytes, false));  // NOLINT
    if (this->rotate_buf_ == nullptr) {
      this->status_set_error(LOG_STR("Memory allocation failure"));
      this->mark_failed();
      return;
    }
#ifdef USE_ESP32_VARIANT_ESP32P4
    ppa_client_config_t ppa_config{};
    ppa_config.oper_type = PPA_OPERATION_SRM;
    ppa_config.max_pending_trans_num = 1;
    if (ppa_register_client(&ppa_config, &this->ppa_client_) != ESP_OK) {
      ESP_LOGW(TAG, "PPA client registration failed, using software rotation");
      this->ppa_client_ = nullptr;
    }
#endif
  }
  // Diagnostic callbacks are independent of optional user draw callbacks.
  // Always register them so startup profiling observes every display refresh.
  lv_display_add_event_cb(this->disp_, profile_refresh_start_cb, LV_EVENT_REFR_START, this);
  lv_display_add_event_cb(this->disp_, profile_layout_ready_cb, LV_EVENT_UPDATE_LAYOUT_COMPLETED, this);
  lv_display_add_event_cb(this->disp_, profile_render_start_cb, LV_EVENT_RENDER_START, this);
  lv_display_add_event_cb(this->disp_, profile_render_end_cb, LV_EVENT_REFR_READY, this);
  if (this->draw_start_callback_ != nullptr) {
    lv_display_add_event_cb(this->disp_, render_start_cb, LV_EVENT_RENDER_START, this);
  }
  if (this->draw_end_callback_ != nullptr || this->update_when_display_idle_) {
    lv_display_add_event_cb(this->disp_, render_end_cb, LV_EVENT_REFR_READY, this);
  }
  this->refr_timer_ = lv_display_get_refr_timer(this->disp_);
  lv_timer_set_period(this->refr_timer_, this->refr_timer_period_);
#if LV_USE_LOG
  lv_log_register_print_cb([](lv_log_level_t level, const char *buf) {
    auto next = strchr(buf, ')');
    if (next != nullptr)
      buf = next + 1;
    while (isspace(*buf))
      buf++;
    if (level >= sizeof(LOG_LEVEL_MAP) / sizeof(LOG_LEVEL_MAP[0]))
      level = sizeof(LOG_LEVEL_MAP) / sizeof(LOG_LEVEL_MAP[0]) - 1;
    esp_log_printf_(LOG_LEVEL_MAP[level], TAG, 0, "%.*s", (int) strlen(buf) - 1, buf);
  });
#endif
  ESP_LOGI(TAG, "LVGL_SETUP_PROFILE phase=configured elapsed_ms=%u",
           static_cast<unsigned>(millis() - setup_started_ms));
  this->show_page(0, LV_SCREEN_LOAD_ANIM_NONE, 0);
  ESP_LOGI(TAG, "LVGL_SETUP_PROFILE phase=ready elapsed_ms=%u",
           static_cast<unsigned>(millis() - setup_started_ms));
  lv_display_trigger_activity(this->disp_);
  this->update_orientation_();
}

void LvglComponent::update() {
  // update indicators
  if (this->is_paused()) {
    return;
  }
  this->idle_callbacks_.call(lv_display_get_inactive_time(this->disp_));
}

void LvglComponent::loop() {
  if (this->paused_) {
    if (this->show_snow_)
      this->write_random_();
    return;
  }
  // Pause/resume the display's own refresh timer to track its busy state. While paused, LVGL
  // still keeps track of invalidated areas but won't render or flush them, so nothing needs to
  // be discarded or replayed: once resumed, the accumulated areas are simply drawn as normal.
  // Input events and other timers keep being processed below regardless of this state.
  if (this->update_when_display_idle_) {
    bool busy = this->displays_busy_();
    if (busy && !this->refr_timer_paused_) {
      this->refr_timer_paused_ = true;
      // calling lv_timer_pause() here would be ineffective; LVGL pauses and resumes the timer based on its own internal
      // state, which is not aware of the display's busy state. Instead, we extend the timer period to avoid it firing
      // while the display is busy.
      lv_timer_set_period(this->refr_timer_, 5 * 60 * 1000);
    } else if (!busy && this->refr_timer_paused_) {
      this->refr_timer_paused_ = false;
      lv_timer_set_period(this->refr_timer_, this->refr_timer_period_);
      // Don't wait for the timer's next natural period: refresh right away now that the
      // display is idle again.
      lv_timer_ready(this->refr_timer_);
    }
  }
  this->refresh_profile_.begin(millis());
  lv_timer_handler();
  this->first_loop_completed_ = true;
  this->log_refresh_profile_();
}

void LvglComponent::log_refresh_profile_() {
  const uint32_t now = millis();
  const auto sample = this->refresh_profile_.end(now);
  if (!this->startup_loop_profile_seq_.observe(now, sample.handler_ms, 50, 8))
    return;
  ESP_LOGI(TAG,
           "LVGL_REFRESH_PROFILE seq=%u handler_ms=%u pre_ms=%u render_ms=%u post_ms=%u render_seen=%s "
           "refresh_paused=%s dispatch_ms=%u layout_ms=%u prepare_ms=%u ready_seen=%s",
           static_cast<unsigned>(this->startup_loop_profile_seq_.sequence),
           static_cast<unsigned>(sample.handler_ms), static_cast<unsigned>(sample.pre_ms),
           static_cast<unsigned>(sample.render_ms), static_cast<unsigned>(sample.post_ms),
           YESNO(sample.render_seen), YESNO(this->refr_timer_paused_),
           static_cast<unsigned>(sample.dispatch_ms), static_cast<unsigned>(sample.layout_ms),
           static_cast<unsigned>(sample.prepare_ms), YESNO(sample.ready_seen));
}

#ifdef USE_LVGL_ANIMIMG
void lv_animimg_stop(lv_obj_t *obj) {
  int32_t duration = lv_animimg_get_duration(obj);
  lv_animimg_set_duration(obj, 0);
  lv_animimg_start(obj);
  lv_animimg_set_duration(obj, duration);
}
#endif
void LvglComponent::static_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p) {
  reinterpret_cast<LvglComponent *>(lv_display_get_user_data(disp_drv))->flush_cb_(disp_drv, area, color_p);
}

#ifdef USE_LVGL_SCALE
/**
 * Function to apply colors to ticks based on position
 * @param e The event data
 * @param color_start The color to apply to the first tick
 * @param color_end  The color to apply to the last tick
 * @param width
 */
void lv_scale_draw_event_cb(lv_event_t *e, int16_t range_start, int16_t range_end, lv_color_t color_start,
                            lv_color_t color_end, int width, bool local) {
  auto *scale = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_draw_task_t *task = lv_event_get_draw_task(e);

  if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_LINE) {
    auto *line_dsc = static_cast<lv_draw_line_dsc_t *>(lv_draw_task_get_draw_dsc(task));
    int tick = line_dsc->base.id2;
    if (tick >= range_start && tick <= range_end) {
      int ratio;
      if (local) {
        int range = range_end - range_start;
        tick -= range_start;
        ratio = range == 0 ? 0 : (tick * 255) / range;
      } else {
        // total tick count is guaranteed to be at least 2.
        ratio = (line_dsc->base.id1 * 255) / (lv_scale_get_total_tick_count(scale) - 1);
      }
      line_dsc->color = lv_color_mix(color_end, color_start, ratio);
      line_dsc->width += width;
    }
  }
}
#endif  // USE_LVGL_SCALE

#ifdef USE_LVGL_GRADIENT
/**
 *
 * @param dsc The gradient descriptor containing the color stops
 * @param pos The current position to calculate the color for
 * @return The color for the given position
 */

lv_color_t lv_grad_calculate_color(const lv_grad_dsc_t *dsc, int32_t pos) {
  if (dsc->stops_count == 0)
    return lv_color_black();
  if (dsc->stops_count == 1 || pos <= dsc->stops[0].frac)
    return dsc->stops[0].color;
  if (pos >= dsc->stops[dsc->stops_count - 1].frac)
    return dsc->stops[dsc->stops_count - 1].color;
  int i = 1;
  while (i < dsc->stops_count && dsc->stops[i].frac < pos)
    i++;
  auto *stop1 = &dsc->stops[i - 1];
  auto *stop2 = &dsc->stops[i];
  int32_t range = stop2->frac - stop1->frac;
  int32_t offset = pos - stop1->frac;
  return lv_color_mix(stop2->color, stop1->color, range == 0 ? 0 : (offset * 255) / range);
}
#endif  // USE_LVGL_GRADIENT

lv_point_t LvglComponent::get_touch_relative_to_obj(lv_obj_t *obj) {
  auto *indev = lv_indev_get_act();
  if (indev == nullptr) {
    return {INT32_MAX, INT32_MAX};
  }
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  point.x -= coords.x1;
  point.y -= coords.y1;
  return point;
}

static void lv_container_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
  LV_TRACE_OBJ_CREATE("begin");
  LV_UNUSED(class_p);
}

// Container class. Name is based on LVGL naming convention but upper case to keep ESPHome clang-tidy happy
const lv_obj_class_t LV_CONTAINER_CLASS = {
    .base_class = &lv_obj_class,
    .constructor_cb = lv_container_constructor,
    .name = "lv_container",
};

lv_obj_t *lv_container_create(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_class_create_obj(&LV_CONTAINER_CLASS, parent);
  lv_obj_class_init_obj(obj);
  return obj;
}

#ifdef USE_LVGL_LIST
int lv_list_get_row_index(lv_obj_t *list, lv_obj_t *child) {
  for (lv_obj_t *obj = child; obj != nullptr; obj = lv_obj_get_parent(obj)) {
    if (lv_obj_get_parent(obj) == list)
      return lv_obj_get_index(obj);
  }
  ESP_LOGW(TAG, "lvgl.list: entry is not inside the list it was added to");
  return -1;
}

lv_obj_t *lv_list_get_row_for_remove(lv_obj_t *list, int index) {
  lv_obj_t *child = index < 0 ? nullptr : lv_obj_get_child(list, index);
  if (child == nullptr) {
    ESP_LOGW(TAG, "lvgl.list.remove: index %d is out of range, ignoring", index);
  }
  return child;
}
#endif  // USE_LVGL_LIST
}  // namespace esphome::lvgl

lv_result_t lv_mem_test_core() { return LV_RESULT_OK; }

void lv_mem_init() {}

void lv_mem_deinit() {}

#if defined(USE_HOST) || defined(USE_RP2) || defined(USE_ESP8266)
void *lv_malloc_core(size_t size) {
  auto *ptr = malloc(size);  // NOLINT
  if (ptr == nullptr) {
    ESP_LOGE(esphome::lvgl::TAG, "Failed to allocate %zu bytes", size);
  }
  return ptr;
}
void lv_free_core(void *ptr) { return free(ptr); }                            // NOLINT
void *lv_realloc_core(void *ptr, size_t size) { return realloc(ptr, size); }  // NOLINT

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) { memset(mon_p, 0, sizeof(lv_mem_monitor_t)); }
static void *lv_alloc_draw_buf(size_t size, bool internal) {
  return malloc(size);  // NOLINT
}

#elif defined(USE_ESP32)
static constexpr unsigned cap_bits = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;  // NOLINT

static void *lv_alloc_draw_buf(size_t size, bool internal) {
  void *buffer;
  size = LV_ROUND_UP(size, LV_DRAW_BUF_ALIGN);
  buffer = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, internal ? MALLOC_CAP_8BIT : cap_bits);  // NOLINT
  if (buffer == nullptr) {
    ESP_LOGW(esphome::lvgl::TAG, "Failed to allocate %zu bytes for %sdraw buffer", size, internal ? "internal " : "");
  }
  return buffer;
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, cap_bits);
  mon_p->total_size = heap_info.total_allocated_bytes + heap_info.total_free_bytes;
  mon_p->free_size = heap_info.total_free_bytes;
  mon_p->max_used = heap_info.total_allocated_bytes;
  mon_p->free_biggest_size = heap_info.largest_free_block;
  mon_p->used_cnt = heap_info.allocated_blocks;
  mon_p->free_cnt = heap_info.free_blocks;
  // An unavailable memory pool may report no blocks at all.
  const auto block_count = heap_info.allocated_blocks + heap_info.free_blocks;
  mon_p->used_pct = block_count > 0 ? heap_info.allocated_blocks * 100 / block_count : 0;
  mon_p->frag_pct = 0;
}

void *lv_malloc_core(size_t size) {
  void *ptr;
  ptr = heap_caps_malloc(size, cap_bits);
  if (ptr == nullptr) {
    // This fallback belongs to this allocation, not to later requests.
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  if (ptr == nullptr) {
    ESP_LOGE(esphome::lvgl::TAG, "Failed to allocate %zu bytes", size);
    return nullptr;
  }
  ESP_LOGV(esphome::lvgl::TAG, "allocate %zu - > %p", size, ptr);
  return ptr;
}

void lv_free_core(void *ptr) {
  ESP_LOGV(esphome::lvgl::TAG, "free %p", ptr);
  if (ptr == nullptr)
    return;
  heap_caps_free(ptr);
}

void *lv_realloc_core(void *ptr, size_t size) {
  ESP_LOGV(esphome::lvgl::TAG, "realloc %p: %zu", ptr, size);
  // A zero-size realloc frees the old block; do not retry a freed pointer.
  if (size == 0) {
    heap_caps_free(ptr);
    return nullptr;
  }
  void *resized = heap_caps_realloc(ptr, size, cap_bits);
  if (resized == nullptr) {
    // Failed realloc preserves the original block for this local fallback.
    resized = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
  }
  return resized;
}
#endif


#endif  // !USE_HOST
