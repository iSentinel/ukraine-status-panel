#include "artwork_decoder.h"
#include "src/libs/tjpgd/tjpgd.h"
#include <algorithm>
#include <cstring>
namespace usp_spotify_artwork_decode {
static_assert(LV_USE_TJPGD == 1 && JD_FORMAT == 0 && JD_USE_SCALE == 0,
              "Pinned LVGL decoder configuration changed");
struct Context {
  const uint8_t *input;
  size_t size;
  size_t cursor;
  uint16_t *pixels;
  Image output;
  bool (*current)(void *);
  void *owner;
};

/** Read only bytes already present in the caller-owned bounded response. */
inline size_t read(JDEC *decoder, uint8_t *destination, size_t count) {
  auto &ctx = *static_cast<Context *>(decoder->device);
  const size_t take = std::min(count, ctx.size - ctx.cursor);
  if (destination && take) std::memcpy(destination, ctx.input + ctx.cursor, take);
  ctx.cursor += take;
  return take;
}

/** Convert each pinned LVGL BGR888 MCU to the caller's fixed RGB565 frame.
 * This is nearest-neighbour scaling, without cropping or aspect distortion.
 * The caller discards the entire destination when decoding is interrupted.
 */
inline int draw(JDEC *decoder, void *bitmap, JRECT *rect) {
  auto &ctx = *static_cast<Context *>(decoder->device);
  if (!bitmap || rect->right >= decoder->width ||
      rect->bottom >= decoder->height || rect->left > rect->right ||
      rect->top > rect->bottom) return 0;
  if (ctx.current && !ctx.current(ctx.owner)) return 0;
  const auto *bgr = static_cast<const uint8_t *>(bitmap);
  for (uint32_t y = rect->top; y <= rect->bottom; ++y) {
    const uint32_t top = y * ctx.output.height / decoder->height;
    const uint32_t bottom = (y + 1) * ctx.output.height / decoder->height;
    for (uint32_t x = rect->left; x <= rect->right; ++x) {
      const uint16_t color = uint16_t((bgr[2] >> 3) << 11) |
          uint16_t((bgr[1] >> 2) << 5) | uint16_t(bgr[0] >> 3);
      bgr += 3;
      const uint32_t left = x * ctx.output.width / decoder->width;
      const uint32_t right = (x + 1) * ctx.output.width / decoder->width;
      for (uint32_t py = top; py < bottom; ++py)
        for (uint32_t px = left; px < right; ++px)
          ctx.pixels[py * ctx.output.width + px] = color;
    }
  }
  return 1;
}

/** Require a complete, bounded baseline JPEG before touching the destination. */
inline Result prepare(JDEC &decoder, Context &ctx, void *workspace) {
  if (!ctx.input || ctx.size < 4 || ctx.size > MAX_COMPRESSED ||
      !workspace || reinterpret_cast<uintptr_t>(workspace) % alignof(uint32_t) ||
      !ctx.pixels || reinterpret_cast<uintptr_t>(ctx.pixels) % alignof(uint16_t))
    return Result::LIMIT;
  if (ctx.input[0] != 0xFF || ctx.input[1] != 0xD8 ||
      ctx.input[ctx.size - 2] != 0xFF || ctx.input[ctx.size - 1] != 0xD9)
    return Result::INVALID;
  const auto rc = jd_prepare(&decoder, read, workspace, WORKSPACE, &ctx);
  if (rc == JDR_FMT3 || rc == JDR_FMT2) return Result::UNSUPPORTED;
  if (rc != JDR_OK) return Result::INVALID;
  if (!decoder.width || !decoder.height || decoder.width > MAX_SOURCE ||
      decoder.height > MAX_SOURCE) return Result::LIMIT;
  return Result::OK;
}

/** Decode into caller-owned buffers; zero dimensions mean no publishable frame.
 * Source, workspace and pixels must not overlap. The caller provides a full
 * MAX_PIXELS frame and WORKSPACE bytes, preferably checked strict PSRAM.
 */
Result decode(const uint8_t *input, size_t size, void *workspace,
                     uint16_t *pixels, size_t pixel_count, Image &image,
                     bool (*current)(void *), void *owner) {
  image = {};
  if (pixel_count < MAX_PIXELS) return Result::LIMIT;
  Context ctx{input, size, 0, pixels, {}, current, owner};
  JDEC decoder{};
  const auto ready = prepare(decoder, ctx, workspace);
  if (ready != Result::OK) return ready;
  const uint32_t longest = std::max(decoder.width, decoder.height);
  ctx.output.width = std::max<uint32_t>(1, uint32_t(decoder.width) * MAX_EDGE / longest);
  ctx.output.height = std::max<uint32_t>(1, uint32_t(decoder.height) * MAX_EDGE / longest);
  const auto rc = jd_decomp(&decoder, draw, 0);
  if (rc == JDR_INTR) return Result::CANCELLED;
  if (rc != JDR_OK) return Result::INVALID;
  if (current && !current(owner)) return Result::CANCELLED;
  image = ctx.output;
  return Result::OK;
}
}  // namespace usp_spotify_artwork_decode
