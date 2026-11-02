#pragma once
#include <array>
#include <cstdint>

namespace usp_spotify_frames {
constexpr uint8_t NONE = 255;
enum class Phase : uint8_t { FREE, WRITING, READY, DISPLAYED, RETIRED, INVALID };
struct Slot {
  uint64_t ticket{0};
  uint16_t width{0}, height{0};
  Phase phase{Phase::FREE};
};
struct Change { uint8_t visible{NONE}, retired{NONE}; };

/** Two fixed pixel buffers are owned by the caller; this tracks their loans.
 * Every method requires the same external state lock. Keep that lock through
 * the main-task LVGL source change. Only main acknowledges RETIRED, after
 * detaching the source and invalidating its LVGL cache. Workers never call LVGL.
 */
class Frames {
 public:
  /** Invalidate unpublished frames but keep writes and display loans alive. */
  void select(uint64_t ticket) {
    if (current_ == ticket) return;
    current_ = ticket;
    for (auto &slot : slots_)
      if (slot.phase == Phase::READY) slot = {};
  }
  /** Never overwrite a frame still used by the display or a cancelled writer. */
  uint8_t begin(uint64_t ticket) {
    if (!ticket || ticket != current_) return NONE;
    for (const auto &slot : slots_)
      if (slot.ticket == ticket && slot.phase != Phase::FREE) return NONE;
    for (uint8_t i = 0; i < slots_.size(); ++i)
      if (slots_[i].phase == Phase::FREE) {
        slots_[i] = {ticket, 0, 0, Phase::WRITING};
        return i;
      }
    return NONE;
  }
  /** A stale writer releases its own loan only when it has actually stopped. */
  bool finish(uint8_t index, uint64_t ticket, uint16_t width, uint16_t height) {
    if (index >= slots_.size()) return false;
    auto &slot = slots_[index];
    if (!ticket || slot.ticket != ticket || slot.phase != Phase::WRITING)
      return false;
    if (ticket != current_ || !width || !height || width > 300 || height > 300)
      slot = {};
    else slot = {ticket, width, height, Phase::READY};
    return true;
  }
  /** Main task swaps loans; it must apply the source change before unlocking. */
  Change present() {
    uint8_t ready = NONE;
    for (uint8_t i = 0; i < slots_.size(); ++i)
      if (slots_[i].phase == Phase::READY && slots_[i].ticket == current_)
        ready = i;
    if (ready == NONE) return {};
    Change change{ready, detach()};
    slots_[ready].phase = Phase::DISPLAYED;
    return change;
  }
  /** Main must hide the old source and drop its cache before acknowledging. */
  uint8_t detach() {
    for (uint8_t i = 0; i < slots_.size(); ++i)
      if (slots_[i].phase == Phase::DISPLAYED) {
        slots_[i].phase = Phase::RETIRED;
        return i;
      }
    return NONE;
  }
  bool acknowledge(uint8_t index, uint64_t ticket) {
    if (index >= slots_.size() || !ticket) return false;
    auto &slot = slots_[index];
    if (slot.phase != Phase::RETIRED || slot.ticket != ticket) return false;
    slot = {};
    return true;
  }
  Slot view(uint8_t index) const {
    return index < slots_.size() ? slots_[index] : Slot{0,0,0,Phase::INVALID};
  }
  bool visible_current() const {
    for (const auto &slot : slots_)
      if (slot.phase == Phase::DISPLAYED && slot.ticket == current_) return true;
    return false;
  }
 private:
  std::array<Slot, 2> slots_{};
  uint64_t current_{0};
};
static_assert(sizeof(Frames) <= 48, "Only buffer ownership metadata belongs here");
}  // namespace usp_spotify_frames
