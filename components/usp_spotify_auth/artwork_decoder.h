#pragma once
#include <cstddef>
#include <cstdint>
namespace usp_spotify_artwork_decode {
constexpr size_t MAX_COMPRESSED = 128 * 1024;
constexpr size_t WORKSPACE = 4096;
constexpr uint16_t MAX_SOURCE = 1024;
constexpr uint16_t MAX_EDGE = 300;
constexpr size_t MAX_PIXELS = size_t(MAX_EDGE) * MAX_EDGE;
enum class Result { OK, INVALID, UNSUPPORTED, LIMIT, CANCELLED };
struct Image { uint16_t width{0}; uint16_t height{0}; };
/** Decode a bounded JPEG into a caller-owned RGB565 frame.
 * Provide a non-overlapping aligned 4096-byte workspace and 90000 pixels.
 * Cancellation or any error leaves zero dimensions: never publish that frame.
 */
Result decode(const uint8_t *input, size_t size, void *workspace,
              uint16_t *pixels, size_t pixel_count, Image &image,
              bool (*current)(void *) = nullptr, void *owner = nullptr);
}  // namespace usp_spotify_artwork_decode
