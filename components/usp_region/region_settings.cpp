#include "region_settings.h"
#include "esphome/core/log.h"
#include "usp_alert_region_ids.h"

namespace esphome::usp_region {
namespace {
constexpr const char *TAG = "usp.region";
/** Allow the 25 map regions plus the separate Kyiv city selection. */
bool selectable(uint16_t id) {
  if (id == 31) return true;
  for (const auto region : usp_alert_rows::REGION_IDS)
    if (region == id) return true;
  return false;
}
}  // namespace

/** Restore the existing key so removing a provider never resets selection. */
void RegionSettings::setup() {
  selected_region_pref_ = global_preferences->make_preference<uint16_t>(
      0x4704A18AU, true);
  uint16_t restored = 31;
  if (selected_region_pref_.load(&restored) && selectable(restored))
    selected_region_id_ = restored;
  ESP_LOGI(TAG, "Selected region restored id=%u", selected_region_id_);
}

/** Queue persistence without flushing unrelated settings. */
bool RegionSettings::save_selected_region_preference_(uint16_t region_id) {
  return selected_region_pref_.save(&region_id);
}

/** Reject invalid IDs and failed saves before changing the live selection. */
bool RegionSettings::select_region(uint16_t next) {
  if (!selectable(next)) return false;
  if (next == selected_region_id_) return true;
  if (!save_selected_region_preference_(next)) {
    ESP_LOGW(TAG, "Selected region preference queue failed id=%u", next);
    return false;
  }
  selected_region_id_ = next;
  ESP_LOGI(TAG, "Selected region queued id=%u", next);
  return true;
}
}  // namespace esphome::usp_region
