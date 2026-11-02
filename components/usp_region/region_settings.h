#pragma once
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <cstdint>

namespace esphome::usp_region {
/** Store the selected region independently of online data providers. */
class RegionSettings : public Component {
 public:
  void setup() override;
  uint16_t selected_region_id() const { return selected_region_id_; }
  bool select_region(uint16_t next);
 protected:
  bool save_selected_region_preference_(uint16_t region_id);
  ESPPreferenceObject selected_region_pref_;
  uint16_t selected_region_id_{31};
};
}  // namespace esphome::usp_region
