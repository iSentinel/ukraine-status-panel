#include "artwork_decoder.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
using namespace usp_spotify_artwork_decode;
struct Buffers {
  alignas(8) std::array<uint8_t, WORKSPACE> workspace{};
  std::array<uint16_t, MAX_PIXELS> pixels{};
};
std::vector<uint8_t> load(const char *name) {
  std::ifstream stream(name, std::ios::binary);
  assert(stream.good());
  return {std::istreambuf_iterator<char>(stream), {}};
}
bool stale(void *) { return false; }
void valid(const char *name, uint16_t width, uint16_t height) {
  auto input = load(name);
  Buffers buffers;
  buffers.pixels.fill(0xBEEF);
  Image image;
  auto rc = decode(input.data(), input.size(), buffers.workspace.data(),
                   buffers.pixels.data(), buffers.pixels.size(), image);
  assert(rc == Result::OK && image.width == width && image.height == height);
  assert(buffers.pixels[0] != 0xBEEF);
  assert(buffers.pixels[width * height - 1] != 0xBEEF);
  for (size_t i = size_t(width) * height; i < MAX_PIXELS; ++i)
    assert(buffers.pixels[i] == 0xBEEF);
}
void reject(const char *name, Result expected) {
  auto input = load(name);
  Buffers buffers; Image image{99,99};
  auto rc = decode(input.data(), input.size(), buffers.workspace.data(),
                   buffers.pixels.data(), buffers.pixels.size(), image);
  assert(rc == expected && !image.width && !image.height);
}
void failures() {
  auto input = load("square.jpg");
  Buffers buffers; Image image{99,99};
  assert(decode(input.data(), input.size(), buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS, image, stale) == Result::CANCELLED);
  assert(!image.width && !image.height);
  assert(decode(input.data(), input.size(), buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS - 1, image) == Result::LIMIT);
  assert(decode(input.data(), input.size(), buffers.workspace.data()+1,
                buffers.pixels.data(), MAX_PIXELS, image) == Result::LIMIT);
  assert(decode(input.data(), MAX_COMPRESSED+1, buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS, image) == Result::LIMIT);
  input.pop_back();
  assert(decode(input.data(), input.size(), buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS, image) == Result::INVALID);
}
struct Cancellation { unsigned remaining; };
bool cancelled_later(void *state) {
  auto &value = *static_cast<Cancellation *>(state);
  return value.remaining-- != 0;
}
void malformed() {
  auto source = load("gradient.jpg");
  Buffers buffers; Image image;
  Cancellation cancellation{7};
  assert(decode(source.data(), source.size(), buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS, image,
                cancelled_later, &cancellation) == Result::CANCELLED);
  assert(!image.width && !image.height);
  uint32_t random = 0x6145;
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    auto input = source;
    random = random * 1664525U + 1013904223U;
    const size_t offset = 2 + random % (input.size() - 4);
    input[offset] ^= uint8_t(1U << (attempt % 8));
    auto rc = decode(input.data(), input.size(), buffers.workspace.data(),
                     buffers.pixels.data(), MAX_PIXELS, image);
    if (rc == Result::OK)
      assert(image.width && image.height && image.width <= 300 && image.height <= 300);
    else assert(!image.width && !image.height);
  }
}

void reference_pixels() {
  auto input = load("gradient.jpg");
  auto reference = load("gradient.rgb");
  assert(reference.size() == 301U * 219U * 3U);
  Buffers buffers; Image image;
  assert(decode(input.data(), input.size(), buffers.workspace.data(),
                buffers.pixels.data(), MAX_PIXELS, image) == Result::OK);
  for (uint32_t y = 0; y < image.height; ++y) {
    const uint32_t sy = ((y + 1) * 219 + image.height - 1) / image.height - 1;
    for (uint32_t x = 0; x < image.width; ++x) {
      const uint32_t sx = ((x + 1) * 301 + image.width - 1) / image.width - 1;
      const auto *rgb = reference.data() + (sy * 301 + sx) * 3;
      const auto pixel = buffers.pixels[y * image.width + x];
      const int r = (pixel >> 11) * 8, g = ((pixel >> 5) & 63) * 4;
      const int b = (pixel & 31) * 8;
      assert(std::abs(r - int(rgb[0] & 248)) <= 8);
      assert(std::abs(g - int(rgb[1] & 252)) <= 8);
      assert(std::abs(b - int(rgb[2] & 248)) <= 8);
    }
  }
}

int main() {
  valid("square.jpg",300,300);
  valid("landscape.jpg",300,150);
  valid("portrait.jpg",150,300);
  valid("gray.jpg",300,300);
  valid("gradient.jpg",300,218);
  valid("gradient-gray.jpg",300,218);
  reject("progressive.jpg",Result::UNSUPPORTED);
  reject("oversize.jpg",Result::LIMIT);
  failures(); malformed(); reference_pixels();
  puts("ARTWORK_DECODER_NATIVE=PASS host_only no_network no_LVGL_calls");
}
