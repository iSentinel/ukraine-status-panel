#include "alarm_manager.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome::usp_alarm {
static const char *const TAG = "usp.alarm";

void AlarmManager::setup() {
  preference_ = global_preferences->make_preference<::usp_alarm_schedule::Store>(
      fnv1_hash("usp_alarm_schedule_v1"), true);
  ::usp_alarm_schedule::Store saved{};
  bool supported = true;
  if (preference_.load(&saved)) {
    for (const auto &record : saved.slots)
      supported &= record.weekdays == 127 || record.weekdays == 62 || record.weekdays == 65;
  } else supported = false;
  const bool loaded = supported && schedule_.restore(saved);
  ESP_LOGI(TAG, "Alarm schedules ready slots=3 loaded=%s enabled=%u%u%u",
           YESNO(loaded), slot(0).enabled, slot(1).enabled, slot(2).enabled);
}

void AlarmManager::dump_config() {
  ESP_LOGCONFIG(TAG, "Alarm schedules: 3 slots, 32-byte storage, Europe/Kyiv");
  ESP_LOGCONFIG(
      TAG, "  No boot/jump catch-up; firing guard attempted before notification");
}

/** Save only explicit configuration changes and actual firing-day guards. */
bool AlarmManager::persist_(const ::usp_alarm_schedule::Store &store) {
  storage_ok_ = preference_.save(&store) && global_preferences->sync();
  if (storage_ok_) status_clear_warning();
  else {
    status_set_warning("Alarm storage unavailable");
    ESP_LOGE(TAG, "Alarm storage failed");
  }
  return storage_ok_;
}

/** Retry only a firing guard that already changed the in-RAM schedule. */
void AlarmManager::queue_firing_guard_retry_() {
  pending_firing_store_ = schedule_.store();
  firing_retry_pending_ = true;
  storage_retry_at_ = millis() + STORAGE_RETRY_MS;
}

/** Bound flash retries to one attempt per interval.
 * Signed subtraction also handles millis rollover.
 */
void AlarmManager::retry_firing_guard_() {
  if (!firing_retry_pending_ ||
      static_cast<int32_t>(millis() - storage_retry_at_) < 0) {
    return;
  }
  if (!persist_(pending_firing_store_)) {
    storage_retry_at_ = millis() + STORAGE_RETRY_MS;
    return;
  }
  firing_retry_pending_ = false;
  ESP_LOGI(TAG, "Alarm firing guard retry committed");
}

/** LCD, Web and Native API callers use this one validated save transaction. */
bool AlarmManager::configure(size_t index, const std::string &text,
                             uint8_t days, bool enabled,
                             uint32_t revision) {
  uint16_t minute = 0;
  auto next = schedule_;
  if (text.size() != 5 || (days != 127 && days != 62 && days != 65) ||
      !::usp_alarm_schedule::parse_time(text.c_str(), &minute) ||
      !next.configure(index, minute, days, enabled)) {
    error_code_ = 1;
    ESP_LOGW(TAG, "Rejected alarm configuration index=%u",
             static_cast<unsigned>(index));
    update_trigger_.trigger();
    return false;
  }
  if (revision != UINT32_MAX && revision != revisions_[index]) {
    error_code_ = 2;
    update_trigger_.trigger();
    return false;
  }
  const bool had_error = error_code_ != 0;
  error_code_ = 0;
  if (std::memcmp(&next.store(), &schedule_.store(), sizeof(next.store())) == 0 &&
      storage_ok_) {
    if (had_error) update_trigger_.trigger();
    ESP_LOGI(TAG, "Alarm unchanged slot=%u enabled=%s",
             static_cast<unsigned>(index + 1), YESNO(enabled));
    return true;
  }
  if (!persist_(next.store())) { update_trigger_.trigger(); return false; }
  schedule_ = next;
  firing_retry_pending_ = false;
  ++revisions_[index];
  ESP_LOGI(TAG, "Alarm configured slot=%u time=%s days=%u enabled=%s",
           static_cast<unsigned>(index + 1), text.c_str(), days, YESNO(enabled));
  update_trigger_.trigger();
  return true;
}

std::string AlarmManager::time_text(size_t index) const {
  if (index >= ::usp_alarm_schedule::SLOT_COUNT) return "--:--";
  const unsigned minute = slot(index).minute;
  if (minute >= 1440) return "--:--";
  char text[6];
  std::snprintf(text, sizeof(text), "%02u:%02u", minute / 60, minute % 60);
  return text;
}

/** Keep the actual fired time visible while the next schedule is edited. */
std::string AlarmManager::notification_time(size_t index) const {
  if (index >= ::usp_alarm_schedule::SLOT_COUNT) return "--:--";
  const unsigned minute = schedule_.active_minute(index);
  if (minute >= 1440) return "--:--";
  char text[6];
  std::snprintf(text, sizeof(text), "%02u:%02u", minute / 60, minute % 60);
  return text;
}

/** An acknowledgement changes only the current notification, not its schedule. */
void AlarmManager::dismiss() {
  if (!schedule_.active()) return;
  schedule_.dismiss();
  update_trigger_.trigger();
  ESP_LOGI(TAG, "Alarm notification dismissed");
}

/** Poll the existing clock; no external requests or synthetic device time. */
void AlarmManager::update() {
  retry_firing_guard_();
  const auto now = clock_->now();
  const ::usp_alarm_schedule::Clock clock{
      {now.year, now.month, now.day_of_month, now.hour, now.minute,
       now.day_of_week, trusted_ && trusted_() && now.is_valid()},
      static_cast<uint32_t>(now.timestamp)};
  const bool valid = usp_clock::valid(clock.local);
  if (valid != clock_valid_) {
    clock_valid_ = valid;
    update_trigger_.trigger();
  }
  const uint8_t due = schedule_.poll(clock);
  if (!due) return;
  const bool persisted = persist_(schedule_.store());
  if (!persisted) queue_firing_guard_retry_();
  else firing_retry_pending_ = false;
  update_trigger_.trigger();
  for (uint8_t index = 0; index < ::usp_alarm_schedule::SLOT_COUNT; ++index) {
    if (!(due & (1U << index))) continue;
    ESP_LOGI(TAG, "Alarm due slot=%u time=%s day=%u persisted=%s",
             index + 1, time_text(index).c_str(),
             static_cast<unsigned>(slot(index).fired_day), YESNO(persisted));
    alarm_trigger_.trigger(index);
  }
}
}  // namespace esphome::usp_alarm

/** Read-only evidence; this action cannot change device time or play a tone. */
void esphome::usp_alarm::AlarmManager::dump_runtime() const {
  ESP_LOGI("usp.alarm",
           "ALARM_STATE active=%u trusted=%s storage=%s retry=%s error=%u",
           active(), YESNO(clock_valid()), YESNO(storage_ok()),
           YESNO(firing_retry_pending_), error_code());
  for (size_t index = 0; index < ::usp_alarm_schedule::SLOT_COUNT; ++index) {
    const auto &record = slot(index);
    ESP_LOGI("usp.alarm",
             "ALARM_SLOT index=%u time=%s days=%u enabled=%u fired_day=%u revision=%u",
             static_cast<unsigned>(index), time_text(index).c_str(),
             record.weekdays, record.enabled,
             static_cast<unsigned>(record.fired_day),
             static_cast<unsigned>(revisions_[index]));
  }
}
