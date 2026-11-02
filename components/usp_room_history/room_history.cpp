#include "room_history.h"

#include "esphome/core/log.h"
#include "esp_heap_caps.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace esphome::usp_room_history {

static const char *const TAG = "usp.room_history";

void RoomHistory::setup() {
  const size_t bytes = this->storage_bytes();
  this->samples_ = static_cast<RoomSample *>(
      heap_caps_calloc(ROOM_HISTORY_CAPACITY, sizeof(RoomSample),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->using_psram_ = this->samples_ != nullptr;

  if (this->samples_ == nullptr) {
    ESP_LOGW(TAG, "PSRAM allocation failed; trying internal heap");
    this->samples_ = static_cast<RoomSample *>(
        heap_caps_calloc(ROOM_HISTORY_CAPACITY, sizeof(RoomSample),
                         MALLOC_CAP_8BIT));
  }

  this->storage_ok_ = this->samples_ != nullptr;
  if (!this->storage_ok_) {
    this->status_set_warning("Room history allocation failed");
    ESP_LOGW(TAG, "Room history disabled; allocation of %u bytes failed",
             static_cast<unsigned>(bytes));
    return;
  }

  this->status_clear_warning();
  ESP_LOGI(TAG, "Room history ready samples=%u bytes=%u storage=%s",
           static_cast<unsigned>(ROOM_HISTORY_CAPACITY),
           static_cast<unsigned>(bytes),
           this->using_psram_ ? "PSRAM" : "internal");
}

void RoomHistory::dump_config() {
  ESP_LOGCONFIG(TAG, "Room history:");
  ESP_LOGCONFIG(TAG, "  Capacity: %u samples / 24h at 5-minute buckets",
                static_cast<unsigned>(ROOM_HISTORY_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Storage bytes: %u",
                static_cast<unsigned>(this->storage_bytes()));
  ESP_LOGCONFIG(TAG, "  Storage: %s",
                !this->storage_ok_ ? "unavailable" :
                (this->using_psram_ ? "PSRAM" : "internal fallback"));
  ESP_LOGCONFIG(TAG, "  Persistence: none (RAM only; no per-sample flash writes)");
}

// Reject invalid values instead of turning them into plausible endpoints.
RoomSample RoomHistory::make_sample_(uint32_t epoch, float temperature_c,
                                      float humidity_pct, bool valid) {
  RoomSample next{};
  next.epoch = epoch;
  next.flags = ROOM_SAMPLE_GAP;
  constexpr float minimum =
      std::numeric_limits<int16_t>::lowest() / 100.0f;
  constexpr float maximum =
      std::numeric_limits<int16_t>::max() / 100.0f;
  if (!valid || !std::isfinite(temperature_c) ||
      !std::isfinite(humidity_pct) || temperature_c < minimum ||
      temperature_c > maximum || humidity_pct < 0.0f ||
      humidity_pct > 100.0f) return next;

  // Bounds are checked before lround, including finite values of huge size.
  next.temperature_centi = static_cast<int16_t>(
      std::lround(static_cast<double>(temperature_c) * 100.0));
  next.humidity_centi = static_cast<uint16_t>(
      std::lround(static_cast<double>(humidity_pct) * 100.0));
  next.flags = ROOM_SAMPLE_VALID;
  return next;
}

bool RoomHistory::record(uint32_t epoch, float temperature_c,
                         float humidity_pct, bool valid) {
  if (!this->storage_ok_ || this->samples_ == nullptr || epoch == 0)
    return false;
  const uint32_t bucket = epoch / ROOM_HISTORY_BUCKET_SECONDS;
  if (this->have_bucket_ && bucket < this->last_bucket_) return false;
  const RoomSample next =
      make_sample_(epoch, temperature_c, humidity_pct, valid);

  if (this->have_bucket_ && bucket == this->last_bucket_ &&
      this->count_ != 0) {
    const size_t latest =
        (this->head_ + ROOM_HISTORY_CAPACITY - 1) % ROOM_HISTORY_CAPACITY;
    // Keep a newer measurement when trusted time moves backwards.
    if (epoch < this->samples_[latest].epoch) return false;
    this->remove_counts_(this->samples_[latest]);
    this->samples_[latest] = next;
    this->add_counts_(next);
    return true;
  }
  this->append_(next);
  this->last_bucket_ = bucket;
  this->have_bucket_ = true;
  return true;
}

// Keep overwrite counters correct without changing the ring's fixed size.
void RoomHistory::append_(const RoomSample &sample) {
  if (this->count_ == ROOM_HISTORY_CAPACITY)
    this->remove_counts_(this->samples_[this->head_]);
  else
    ++this->count_;
  this->samples_[this->head_] = sample;
  this->add_counts_(sample);
  this->head_ = (this->head_ + 1) % ROOM_HISTORY_CAPACITY;
}

void RoomHistory::clear() {
  if (this->samples_ != nullptr) {
    std::fill_n(this->samples_, ROOM_HISTORY_CAPACITY, RoomSample{});
  }
  this->head_ = 0;
  this->count_ = 0;
  this->valid_count_ = 0;
  this->gap_count_ = 0;
  this->last_bucket_ = 0;
  this->have_bucket_ = false;
}

const RoomSample *RoomHistory::sample(size_t oldest_index) const {
  if (this->samples_ == nullptr || oldest_index >= this->count_) {
    return nullptr;
  }
  const size_t index =
      (this->oldest_index_() + oldest_index) % ROOM_HISTORY_CAPACITY;
  return &this->samples_[index];
}

size_t RoomHistory::oldest_index_() const {
  return (this->head_ + ROOM_HISTORY_CAPACITY - this->count_) %
         ROOM_HISTORY_CAPACITY;
}

void RoomHistory::remove_counts_(const RoomSample &sample) {
  if ((sample.flags & ROOM_SAMPLE_VALID) != 0 && this->valid_count_ != 0) {
    --this->valid_count_;
  }
  if ((sample.flags & ROOM_SAMPLE_GAP) != 0 && this->gap_count_ != 0) {
    --this->gap_count_;
  }
}

void RoomHistory::add_counts_(const RoomSample &sample) {
  if ((sample.flags & ROOM_SAMPLE_VALID) != 0) {
    ++this->valid_count_;
  }
  if ((sample.flags & ROOM_SAMPLE_GAP) != 0) {
    ++this->gap_count_;
  }
}

}  // namespace esphome::usp_room_history
