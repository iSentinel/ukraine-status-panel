#pragma once
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <cstddef>
#include <cstdint>

namespace usp_spotify_artwork_buffers {
constexpr size_t COMPRESSED_BYTES = 128 * 1024;
constexpr size_t WORKSPACE_BYTES = 4096;
constexpr size_t FRAME_PIXELS = 300 * 300;
constexpr size_t FRAME_BYTES = FRAME_PIXELS * sizeof(uint16_t);
constexpr size_t TOTAL_BYTES = COMPRESSED_BYTES + WORKSPACE_BYTES + 2 * FRAME_BYTES;
static_assert(TOTAL_BYTES == 495168, "Artwork memory budget changed");

/** A lazy allocation in external RAM only. No internal-RAM fallback is allowed.
 * The owner must stop the writer and detach all display loans before destruction.
 * Calls require the same external lock as frame metadata. This class owns no task.
 */
class Buffers {
 public:
  Buffers() = default;
  Buffers(const Buffers &) = delete;
  Buffers &operator=(const Buffers &) = delete;
  ~Buffers() { if (storage_) heap_caps_free(storage_); }

  /** Keep a successful allocation stable for the entire component lifetime. */
  bool ensure() {
    if (storage_) return true;
    auto *candidate = static_cast<uint8_t *>(heap_caps_malloc(
        TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!candidate) return false;
    if (!esp_ptr_external_ram(candidate) ||
        !esp_ptr_external_ram(candidate + TOTAL_BYTES - 1) ||
        reinterpret_cast<uintptr_t>(candidate) % alignof(uint32_t)) {
      heap_caps_free(candidate);
      return false;
    }
    storage_ = candidate;
    return true;
  }
  uint8_t *compressed() { return storage_; }
  void *workspace() { return storage_ ? storage_ + COMPRESSED_BYTES : nullptr; }
  uint16_t *frame(uint8_t index) {
    if (!storage_ || index >= 2) return nullptr;
    return reinterpret_cast<uint16_t *>(storage_ + COMPRESSED_BYTES +
        WORKSPACE_BYTES + size_t(index) * FRAME_BYTES);
  }
 private:
  uint8_t *storage_{nullptr};
};
}  // namespace usp_spotify_artwork_buffers
