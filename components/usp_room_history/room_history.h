#pragma once

#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome::usp_room_history {

static constexpr size_t ROOM_HISTORY_CAPACITY = 288;
static constexpr uint32_t ROOM_HISTORY_BUCKET_SECONDS = 300;

enum RoomSampleFlags : uint8_t {
  ROOM_SAMPLE_VALID = 0x01,
  ROOM_SAMPLE_GAP = 0x02,
};

struct RoomSample {
  uint32_t epoch{0};
  int16_t temperature_centi{0};
  uint16_t humidity_centi{0};
  uint8_t flags{0};
  uint8_t reserved{0};
};

static_assert(sizeof(RoomSample) == 12, "RoomSample layout changed");

class RoomHistory final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  bool record(uint32_t epoch, float temperature_c, float humidity_pct,
              bool valid);
  void clear();

  const RoomSample *sample(size_t oldest_index) const;

  size_t capacity() const { return ROOM_HISTORY_CAPACITY; }
  size_t count() const { return this->count_; }
  size_t valid_count() const { return this->valid_count_; }
  size_t gap_count() const { return this->gap_count_; }
  size_t storage_bytes() const {
    return ROOM_HISTORY_CAPACITY * sizeof(RoomSample);
  }
  bool using_psram() const { return this->using_psram_; }
  bool storage_ok() const { return this->storage_ok_; }

 protected:
  static RoomSample make_sample_(uint32_t epoch, float temperature_c,
                                 float humidity_pct, bool valid);
  void append_(const RoomSample &sample);
  size_t oldest_index_() const;
  void remove_counts_(const RoomSample &sample);
  void add_counts_(const RoomSample &sample);

  RoomSample *samples_{nullptr};
  size_t head_{0};
  size_t count_{0};
  size_t valid_count_{0};
  size_t gap_count_{0};
  uint32_t last_bucket_{0};
  bool have_bucket_{false};
  bool using_psram_{false};
  bool storage_ok_{false};
};

}  // namespace esphome::usp_room_history
