#include <lvgl.h>
#include "display/lv_display_private.h"
#include "draw/lv_draw_buf_private.h"
#include "lvgl_native_copy_policy.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using esphome::lvgl::NativeCopyRegion;
using esphome::lvgl::native_copy_plan;

namespace {
constexpr int WIDTH = 1024;
constexpr int HEIGHT = 600;
constexpr size_t PIXELS = size_t(WIDTH) * HEIGHT;
constexpr size_t FRAME_BYTES = PIXELS * sizeof(uint16_t);
constexpr size_t DMA_THRESHOLD = 64U * 1024U;

lv_draw_buf_handlers_t handlers{};
lv_draw_buf_copy_cb_t fallback_copy{};
const void *buffer_1{};
const void *buffer_2{};
unsigned custom_calls{};
unsigned dma_routes{};
unsigned fallback_routes{};
size_t dma_bytes{};

void flush_ready(lv_display_t *display, const lv_area_t *, uint8_t *) {
  lv_display_flush_ready(display);
}

bool same_area(const lv_area_t *left, const lv_area_t *right) {
  return left != nullptr && right != nullptr && left->x1 == right->x1 &&
         left->y1 == right->y1 && left->x2 == right->x2 &&
         left->y2 == right->y2;
}

bool native_pair(const lv_draw_buf_t *dest, const lv_draw_buf_t *src) {
  return (dest->data == buffer_1 && src->data == buffer_2) ||
         (dest->data == buffer_2 && src->data == buffer_1);
}

void counted_copy(lv_draw_buf_t *dest, const lv_area_t *dest_area,
                  const lv_draw_buf_t *src, const lv_area_t *src_area) {
  custom_calls++;
  if (native_pair(dest, src) && same_area(dest_area, src_area)) {
    const NativeCopyRegion area{
        dest_area->x1, dest_area->y1, dest_area->x2, dest_area->y2};
    const auto plan = native_copy_plan(
        area, WIDTH, HEIGHT, dest->header.stride, sizeof(uint16_t),
        DMA_THRESHOLD);
    if (plan.use_dma && src->header.stride == dest->header.stride) {
      std::memcpy(dest->data + plan.offset_bytes,
                  src->data + plan.offset_bytes, plan.copy_bytes);
      dma_routes++;
      dma_bytes += plan.copy_bytes;
      return;
    }
  }
  fallback_routes++;
  fallback_copy(dest, dest_area, src, src_area);
}

void set_screen_color(lv_obj_t *screen, uint32_t color) {
  lv_obj_set_style_bg_color(screen, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}
}  // namespace

int main() {
  lv_init();
  auto *display = lv_display_create(WIDTH, HEIGHT);
  assert(display != nullptr);
  lv_display_set_default(display);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  std::vector<uint16_t> first_buffer(PIXELS, 0x1111U);
  std::vector<uint16_t> second_buffer(PIXELS, 0x2222U);
  lv_display_set_buffers(display, first_buffer.data(), second_buffer.data(),
                         FRAME_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display, flush_ready);

  handlers = *display->buf_1->handlers;
  fallback_copy = handlers.buf_copy_cb;
  assert(fallback_copy != nullptr);
  buffer_1 = first_buffer.data();
  buffer_2 = second_buffer.data();
  handlers.buf_copy_cb = counted_copy;
  display->buf_1->handlers = &handlers;
  display->buf_2->handlers = &handlers;

  auto *first = lv_obj_create(nullptr);
  auto *second = lv_obj_create(nullptr);
  set_screen_color(first, 0x112233U);
  set_screen_color(second, 0x445566U);
  lv_screen_load(first);
  lv_refr_now(display);
  lv_screen_load(second);
  lv_refr_now(display);

  custom_calls = 0;
  dma_routes = 0;
  fallback_routes = 0;
  dma_bytes = 0;
  const bool differ_before =
      std::memcmp(first_buffer.data(), second_buffer.data(), FRAME_BYTES) != 0;
  lv_refr_now(display);
  const bool equal_after =
      std::memcmp(first_buffer.data(), second_buffer.data(), FRAME_BYTES) == 0;

  assert(differ_before);
  assert(equal_after);
  assert(custom_calls == 1U);
  assert(dma_routes == 1U);
  assert(fallback_routes == 0U);
  assert(dma_bytes == FRAME_BYTES);
  std::printf(
      "LVGL_DIRECT_SYNC_COPY=PASS custom=%u dma=%u fallback=%u bytes=%zu\n",
      custom_calls, dma_routes, fallback_routes, dma_bytes);
  lv_display_delete(display);
  lv_deinit();
  return 0;
}
