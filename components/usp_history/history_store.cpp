#include "history_store.h"

#include <cstdio>
#include <cstring>
#include <limits>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "usp_ui_i18n.h"

namespace esphome::usp_history {

static const char *const TAG = "usp.history";
static constexpr uint32_t HISTORY_MAGIC = 0x48505355;
static constexpr uint16_t HISTORY_VERSION = 1;

namespace {

int16_t clamp_int16(int value) {
  if (value > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (value < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(value);
}

}  // namespace

void HistoryStore::setup() {
  const uint32_t key = fnv1_hash("usp_history_v1");
  this->preference_ =
      global_preferences->make_preference<HistoryBlob>(key, true);
  this->load_();
  ESP_LOGI(TAG, "History ready count=%u total=%lu loaded=%s storage=%s",
           static_cast<unsigned>(this->blob_.count),
           static_cast<unsigned long>(this->blob_.total_events),
           YESNO(this->loaded_from_nvs_), YESNO(this->storage_ok_));
}

void HistoryStore::loop() {
  if (!this->dirty_) return;
  const uint32_t now = millis();
  if (this->save_failed_) {
    if (!this->retry_due_(now)) return;
  } else if (now - this->dirty_since_ms_ < this->commit_delay_ms_) {
    return;
  }
  this->save_();
}

void HistoryStore::on_shutdown() {
  if (!this->dirty_) {
    ESP_LOGD(TAG, "Shutdown flush skipped: history clean");
    return;
  }

  ESP_LOGI(TAG, "Shutdown flush start count=%u total=%lu",
           static_cast<unsigned>(this->blob_.count),
           static_cast<unsigned long>(this->blob_.total_events));
  if (!this->save_(false)) {
    ESP_LOGE(TAG, "Shutdown flush failed");
    return;
  }
  ESP_LOGI(TAG, "Shutdown flush complete");
}

void HistoryStore::dump_config() {
  ESP_LOGCONFIG(TAG, "Persistent Semantic History");
  ESP_LOGCONFIG(TAG, "  Capacity: %u", static_cast<unsigned>(HISTORY_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Blob size: %u", static_cast<unsigned>(sizeof(HistoryBlob)));
  ESP_LOGCONFIG(TAG, "  Commit delay: %lu ms",
                static_cast<unsigned long>(this->commit_delay_ms_));
  ESP_LOGCONFIG(TAG, "  Count: %u", static_cast<unsigned>(this->blob_.count));
}

bool HistoryStore::record_energy(uint16_t region, uint8_t status) {
  return this->record_(EventType::ENERGY, region, status, 0, 0);
}

bool HistoryStore::record_threat(
    uint16_t region, uint8_t state, uint16_t flags) {
  return this->record_(EventType::THREAT, region, state,
                       static_cast<int16_t>(flags), 0);
}

bool HistoryStore::record_schedule(
    uint8_t group, int today_minutes, int tomorrow_minutes, uint8_t subgroup) {
  if (group == 0 || subgroup > 2) return false;
  return this->record_(EventType::SCHEDULE, group, today_minutes,
                       tomorrow_minutes, subgroup);
}

/** Parse only the exact bounded group label returned by the outage client. */
bool HistoryStore::record_schedule(
    const std::string &group, int today_minutes, int tomorrow_minutes) {
  const size_t dot = group.find('.');
  if (dot == std::string::npos || dot == 0 || dot > 3 ||
      dot + 2 != group.size() || (dot > 1 && group.front() == '0')) {
    return false;
  }
  unsigned number = 0;
  for (size_t index = 0; index < dot; ++index) {
    if (group[index] < '0' || group[index] > '9') return false;
    number = number * 10U + static_cast<unsigned>(group[index] - '0');
  }
  const char subgroup = group[dot + 1];
  if (number == 0 || number > 255 || subgroup < '1' || subgroup > '2') {
    return false;
  }
  return this->record_schedule(static_cast<uint8_t>(number), today_minutes,
      tomorrow_minutes, static_cast<uint8_t>(subgroup - '0'));
}

bool HistoryStore::qa_record(
    uint8_t kind, uint16_t region, int value, int aux) {
  if (kind == static_cast<uint8_t>(EventType::ENERGY)) {
    return this->record_energy(region, static_cast<uint8_t>(value));
  }
  if (kind == static_cast<uint8_t>(EventType::THREAT)) {
    return this->record_threat(region, static_cast<uint8_t>(value),
                               static_cast<uint16_t>(aux));
  }
  if (kind == static_cast<uint8_t>(EventType::SCHEDULE)) {
    return this->record_schedule(static_cast<uint8_t>(region), value, aux);
  }
  return false;
}

void HistoryStore::qa_mark_dirty_for_shutdown_probe() {
  this->mark_dirty_();
  ESP_LOGI(TAG, "QA marked history dirty without data mutation count=%u total=%lu",
           static_cast<unsigned>(this->blob_.count),
           static_cast<unsigned long>(this->blob_.total_events));
}


bool HistoryStore::record_(EventType type, uint16_t region, int value,
                           int aux, uint8_t flags) {
  HistoryEvent event{};
  event.epoch = this->now_epoch_();
  if (event.epoch == 0) return false;
  event.region = region;
  event.value = clamp_int16(value);
  event.aux = clamp_int16(aux);
  event.type = static_cast<uint8_t>(type);
  event.flags = flags;
  if (this->is_duplicate_(event)) return false;

  this->blob_.events[this->blob_.head] = event;
  this->blob_.head = (this->blob_.head + 1) % HISTORY_CAPACITY;
  if (this->blob_.count < HISTORY_CAPACITY) ++this->blob_.count;
  ++this->blob_.total_events;
  this->mark_dirty_();
  this->update_trigger_.trigger();
  return true;
}

bool HistoryStore::is_duplicate_(const HistoryEvent &event) const {
  for (size_t index = 0; index < this->blob_.count; ++index) {
    const HistoryEvent *previous = this->event_at_(index);
    if (previous == nullptr) break;
    if (previous->type != event.type || previous->region != event.region) {
      continue;
    }
    return previous->value == event.value && previous->aux == event.aux &&
           previous->flags == event.flags;
  }
  return false;
}

const HistoryEvent *HistoryStore::latest_() const {
  return this->event_at_(0);
}

const HistoryEvent *HistoryStore::event_at_(size_t newest_index) const {
  if (newest_index >= this->blob_.count) return nullptr;
  const size_t offset = newest_index + 1;
  const size_t index =
      (this->blob_.head + HISTORY_CAPACITY - offset) % HISTORY_CAPACITY;
  return &this->blob_.events[index];
}

void HistoryStore::reset_blob_() {
  std::memset(&this->blob_, 0, sizeof(this->blob_));
  this->blob_.magic = HISTORY_MAGIC;
  this->blob_.version = HISTORY_VERSION;
  this->blob_.checksum = this->checksum_(this->blob_);
}

bool HistoryStore::load_() {
  HistoryBlob candidate{};
  if (!this->preference_.load(&candidate)) {
    this->reset_blob_();
    this->storage_ok_ = true;
    this->loaded_from_nvs_ = false;
    return true;
  }
  if (!this->valid_blob_(candidate)) {
    ESP_LOGW(TAG, "Persisted history failed validation; starting empty");
    this->reset_blob_();
    this->storage_ok_ = false;
    this->loaded_from_nvs_ = false;
    return false;
  }
  this->blob_ = candidate;
  this->storage_ok_ = true;
  this->loaded_from_nvs_ = true;
  return true;
}

bool HistoryStore::save_(bool notify) {
  const uint32_t now = millis();
  this->blob_.checksum = this->checksum_(this->blob_);
  const bool queued = this->preference_.save(&this->blob_);
  const bool synced = queued && global_preferences->sync();
  this->storage_ok_ = synced;
  if (!synced) {
    this->note_save_failure_(now);
    ESP_LOGW(TAG, "History NVS save failed; retry in %lu ms",
             static_cast<unsigned long>(this->retry_delay_ms_));
    return false;
  }
  this->dirty_ = false;
  this->reset_save_retry_();
  ++this->save_count_;
  ESP_LOGI(TAG, "History saved count=%u total=%lu writes=%lu",
           static_cast<unsigned>(this->blob_.count),
           static_cast<unsigned long>(this->blob_.total_events),
           static_cast<unsigned long>(this->save_count_));
  if (notify) this->update_trigger_.trigger();
  return true;
}

void HistoryStore::mark_dirty_() {
  if (!this->dirty_) this->dirty_since_ms_ = millis();
  this->dirty_ = true;
}

bool HistoryStore::retry_due_(uint32_t now) const {
  return now - this->last_save_attempt_ms_ >= this->retry_delay_ms_;
}

void HistoryStore::note_save_failure_(uint32_t now) {
  if (this->save_failed_) {
    const uint32_t doubled = this->retry_delay_ms_ * 2U;
    this->retry_delay_ms_ = doubled > MAX_RETRY_MS ? MAX_RETRY_MS : doubled;
  } else {
    this->retry_delay_ms_ = MIN_RETRY_MS;
  }
  this->last_save_attempt_ms_ = now;
  this->save_failed_ = true;
}

void HistoryStore::reset_save_retry_() {
  this->last_save_attempt_ms_ = 0;
  this->retry_delay_ms_ = MIN_RETRY_MS;
  this->save_failed_ = false;
}

bool HistoryStore::flush() {
  if (!this->dirty_) return true;
  return this->save_();
}

void HistoryStore::clear() {
  this->reset_blob_();
  this->dirty_ = true;
  this->dirty_since_ms_ = millis();
  this->save_();
  this->update_trigger_.trigger();
}

uint32_t HistoryStore::now_epoch_() const {
  if (this->clock_ == nullptr) return 0;
  const ESPTime now = this->clock_->now();
  if (!now.is_valid()) return 0;
  return static_cast<uint32_t>(now.timestamp);
}

uint32_t HistoryStore::checksum_(const HistoryBlob &blob) {
  const auto *data = reinterpret_cast<const uint8_t *>(&blob);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < offsetof(HistoryBlob, checksum); ++index) {
    hash ^= data[index];
    hash *= 16777619UL;
  }
  return hash;
}

bool HistoryStore::valid_blob_(const HistoryBlob &blob) {
  if (blob.magic != HISTORY_MAGIC || blob.version != HISTORY_VERSION) {
    return false;
  }
  if (blob.count > HISTORY_CAPACITY || blob.head >= HISTORY_CAPACITY) {
    return false;
  }
  return blob.checksum == checksum_(blob);
}

std::string HistoryStore::time_prefix_(uint32_t epoch) {
  if (epoch == 0) return "--.-- --:--";
  const ESPTime time = ESPTime::from_epoch_local(epoch);
  if (!time.is_valid()) return "--.-- --:--";
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02d.%02d %02d:%02d",
                time.day_of_month, time.month, time.hour, time.minute);
  return buffer;
}

std::string HistoryStore::energy_text_(int value, uint8_t language) {
  if (language == 0) {
    if (value == 3) return "Мережа: достатньо";
    if (value == 4) return "Мережа: дефіцит";
    if (value == 9) return "Мережа: відключення";
    if (value == 10) return "Мережа: значний дефіцит";
    return "Мережа: невідомо";
  }
  if (language == 1) {
    if (value == 3) return "Сеть: достаточно";
    if (value == 4) return "Сеть: дефицит";
    if (value == 9) return "Сеть: отключение";
    if (value == 10) return "Сеть: сильный дефицит";
    return "Сеть: неизвестно";
  }
  if (value == 3) return "Grid: sufficient";
  if (value == 4) return "Grid: deficit";
  if (value == 9) return "Grid: outage";
  if (value == 10) return "Grid: major deficit";
  return "Grid: unknown";
}

std::string HistoryStore::threat_text_(int value, uint8_t language) {
  if (language == 0) {
    if (value == 1) return "Загроза: відбій";
    if (value == 2) return "Загроза: БПЛА";
    if (value == 8) return "Загроза: повітряна загроза";
    if (value == 64) return "Загроза: балістика";
    if (value == 128) return "Загроза: КАБ";
    if (value == 4) return "Загроза: ракети";
    if (value == 16) return "Загроза: тривога";
    if (value == 32) return "Загроза: вибухи";
    return "Загроза: невідомо";
  }
  if (language == 1) {
    if (value == 1) return "Угроза: отбой";
    if (value == 2) return "Угроза: БПЛА";
    if (value == 8) return "Угроза: воздушная угроза";
    if (value == 64) return "Угроза: баллистика";
    if (value == 128) return "Угроза: КАБ";
    if (value == 4) return "Угроза: ракеты";
    if (value == 16) return "Угроза: тревога";
    if (value == 32) return "Угроза: взрывы";
    return "Угроза: неизвестно";
  }
  if (value == 1) return "Threat: clear";
  if (value == 2) return "Threat: drone";
  if (value == 8) return "Threat: air threat";
  if (value == 64) return "Threat: ballistic";
  if (value == 128) return "Threat: guided bomb";
  if (value == 4) return "Threat: missile";
  if (value == 16) return "Threat: air alert";
  if (value == 32) return "Threat: explosion";
  return "Threat: unknown";
}

/** Old records never stored a subgroup; do not invent ".1" for them. */
std::string HistoryStore::schedule_group_text_(
    const HistoryEvent &event, uint8_t language) {
  char group[12];
  if (event.flags == 1 || event.flags == 2) {
    std::snprintf(group, sizeof(group), "%u.%u", event.region, event.flags);
  } else {
    std::snprintf(group, sizeof(group), "%u.?", event.region);
  }
  return usp_ui_i18n::outage_group_label(group, true, language);
}

std::string HistoryStore::schedule_text_(
    const HistoryEvent &event, uint8_t language) {
  const std::string today =
      usp_ui_i18n::duration_minutes(event.value, language, true);
  const std::string tomorrow =
      usp_ui_i18n::duration_minutes(event.aux, language, true);
  if (language == 0) return "Графік: " + today + "/" + tomorrow;
  if (language == 1) return "График: " + today + "/" + tomorrow;
  return "Schedule: " + today + "/" + tomorrow;
}

std::string HistoryStore::compact_duration_(int minutes) {
  if (minutes < 0 || minutes > 24 * 60) return "--:--";
  char value[8];
  std::snprintf(value, sizeof(value), "%02d:%02d", minutes / 60,
                minutes % 60);
  return value;
}

std::string HistoryStore::schedule_text_compact_(
    const HistoryEvent &event, uint8_t language) {
  const std::string today = compact_duration_(event.value);
  const std::string tomorrow = compact_duration_(event.aux);
  if (language == 0) return "Графік: " + today + "/" + tomorrow;
  if (language == 1) return "График: " + today + "/" + tomorrow;
  return "Schedule: " + today + "/" + tomorrow;
}

std::string HistoryStore::event_body_(
    const HistoryEvent &event, uint8_t language) {
  const auto type = static_cast<EventType>(event.type);
  if (type == EventType::ENERGY) {
    return energy_text_(event.value, language);
  }
  if (type == EventType::THREAT) {
    return threat_text_(event.value, language);
  }
  if (type == EventType::SCHEDULE) {
    return schedule_text_(event, language);
  }
  if (language == 0) return "Невідома подія";
  if (language == 1) return "Неизвестное событие";
  return "Unknown event";
}

std::string HistoryStore::event_summary(
    size_t newest_index, uint8_t language) const {
  const HistoryEvent *event = this->event_at_(newest_index);
  if (event == nullptr) {
    if (language == 0) return "Немає подій";
    if (language == 1) return "Нет событий";
    return "No events";
  }
  std::string context;
  if (event->type == static_cast<uint8_t>(EventType::SCHEDULE)) {
    context = " " + schedule_group_text_(*event, language) + " ";
  } else {
    char region[16];
    std::snprintf(region, sizeof(region), " #%u ", event->region);
    context = region;
  }
  return time_prefix_(event->epoch) + context + event_body_(*event, language);
}

std::string HistoryStore::event_summary_compact(
    size_t newest_index, uint8_t language) const {
  const HistoryEvent *event = this->event_at_(newest_index);
  if (event == nullptr ||
      event->type != static_cast<uint8_t>(EventType::SCHEDULE)) {
    return this->event_summary(newest_index, language);
  }
  return time_prefix_(event->epoch) + " " +
      schedule_group_text_(*event, language) + " " +
      schedule_text_compact_(*event, language);
}

std::string HistoryStore::count_text(uint8_t language) const {
  const std::string count = std::to_string(this->blob_.count);
  if (language == 0) return count + " ПОДІЙ";
  if (language == 1) return count + " СОБЫТИЙ";
  return count + " EVENTS";
}

std::string HistoryStore::quality_text(uint8_t language) const {
  if (!this->storage_ok_) {
    if (language == 0) return "NVS · ПОМИЛКА";
    if (language == 1) return "NVS · ОШИБКА";
    return "NVS ERROR";
  }
  if (this->loaded_from_nvs_) {
    if (language == 0) return "NVS OK · ВІДНОВЛЕНО";
    if (language == 1) return "NVS OK · ВОССТАНОВЛЕНО";
    return "NVS OK · RESTORED";
  }
  if (language == 0) return "NVS OK · НОВА";
  if (language == 1) return "NVS OK · НОВАЯ";
  return "NVS OK · NEW";
}

std::string HistoryStore::quality_detail(uint8_t language) const {
  const std::string writes = std::to_string(this->save_count_);
  if (language == 0) return "32 макс. · записів " + writes;
  if (language == 1) return "32 макс. · записей " + writes;
  return "32 max · writes " + writes;
}

void HistoryStore::dump_runtime() const {
  ESP_LOGI(TAG, "HISTORY count=%u total=%lu dirty=%s storage=%s loaded=%s writes=%lu",
           static_cast<unsigned>(this->blob_.count),
           static_cast<unsigned long>(this->blob_.total_events),
           YESNO(this->dirty_), YESNO(this->storage_ok_),
           YESNO(this->loaded_from_nvs_),
           static_cast<unsigned long>(this->save_count_));
  const size_t limit = this->blob_.count < 5 ? this->blob_.count : 5;
  for (size_t index = 0; index < limit; ++index) {
    ESP_LOGI(TAG, "HISTORY_EVENT[%u] %s", static_cast<unsigned>(index),
             this->event_summary(index, 2).c_str());
  }
}

}  // namespace esphome::usp_history
