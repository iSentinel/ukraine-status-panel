#include "outage_client.h"
#include "outage_calendar.h"
#include "outage_group_scanner.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <algorithm>
#include <cstdio>
#include <utility>

namespace esphome::usp_outage {

static const char *const TAG = "usp.outage";
static constexpr char USER_AGENT[] =
    "UkraineStatusPanel/" ESPHOME_PROJECT_VERSION;
static constexpr char PLANNED_URL[] =
    "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/25/"
    "dsos/902/planned-outages";
static constexpr char PROBABLE_URL[] =
    "https://app.yasno.ua/api/blackout-service/public/shutdowns/"
    "probable-outages?regionId=25&dsoId=902";

namespace {

struct GroupObjectCollector {
  static constexpr size_t MAX_OBJECT = 4096;
  char *object{nullptr};
  GroupObjectScanner *scanner{nullptr};

  GroupObjectCollector(const char *const *path, size_t path_len) {
    this->object = static_cast<char *>(heap_caps_malloc(
        MAX_OBJECT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->object != nullptr) {
      this->scanner = new GroupObjectScanner(
          path, path_len, this->object, MAX_OBJECT);
    }
  }

  ~GroupObjectCollector() {
    delete this->scanner;
    if (this->object != nullptr) heap_caps_free(this->object);
  }

  bool consume(char ch) {
    return this->scanner != nullptr && this->scanner->consume(ch);
  }

  bool found() const {
    return this->scanner != nullptr && this->scanner->found();
  }

  bool overflow() const {
    return this->scanner != nullptr && this->scanner->overflow();
  }

  size_t length() const {
    return this->scanner == nullptr ? 0 : this->scanner->length();
  }
};

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

std::string safe_text(JsonVariantConst value) {
  const char *text = value | "";
  return text == nullptr ? std::string{} : std::string{text};
}

size_t group_object_path(OutageWireSchema schema, const char *key,
                         std::array<const char *, 5> *path) {
  if (path == nullptr || key == nullptr) return 0;
  const char *planned_path[] = {key};
  const char *probable_path[] = {"25", "dsos", "902", "groups", key};
  if (schema == OutageWireSchema::PLANNED) {
    std::copy(std::begin(planned_path), std::end(planned_path), path->begin());
    return std::size(planned_path);
  }
  if (schema == OutageWireSchema::PROBABLE) {
    std::copy(std::begin(probable_path), std::end(probable_path), path->begin());
    return std::size(probable_path);
  }
  return 0;
}

struct GroupHttpContext {
  GroupObjectCollector *collector{nullptr};
  usp_http_retry_after::HeaderCapture retry_after{};
};

esp_err_t collect_group_object(esp_http_client_event_t *event) {
  auto *context = static_cast<GroupHttpContext *>(event->user_data);
  if (context == nullptr) return ESP_OK;
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    context->retry_after.observe(event->header_key, event->header_value);
    return ESP_OK;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  if (context->collector == nullptr) return ESP_FAIL;
  const auto *data = static_cast<const char *>(event->data);
  for (int index = 0; index < event->data_len; ++index) {
    if (!context->collector->consume(data[index])) return ESP_FAIL;
  }
  return ESP_OK;
}

}  // namespace

void OutageClient::setup() {
  this->group_pref_ =
      global_preferences->make_preference<StoredGroup>(GROUP_PREF_KEY, true);
  this->load_group_preference_();
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;
  // Place the HTTPS worker stack in PSRAM. RSA certificate verification
  // needs a sufficiently large contiguous internal-DRAM block; keeping this
  // 12 KiB stack internal can starve MbedTLS under multi-client HTTPS load.
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_outage", 12288, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_task_ = nullptr;
    this->mark_failed();
    ESP_LOGE(TAG, "Outage worker could not start");
    return;
  }
  ESP_LOGI(TAG, "Outage worker started group=%u",
           static_cast<unsigned>(this->selected_group_()));
}

void OutageClient::loop() {
  this->check_calendar_();
  if (this->update_pending_.exchange(false)) {
    this->update_trigger_.trigger();
    this->log_snapshot_();
  }
  const bool stale = this->schedule_stale();
  if (stale != this->last_stale_state_) {
    this->last_stale_state_ = stale;
    this->update_trigger_.trigger();
    ESP_LOGI(TAG, "Outage stale state=%s", YESNO(stale));
  }
}

void OutageClient::dump_config() {
  ESP_LOGCONFIG(TAG, "Yasno Outage Client");
  ESP_LOGCONFIG(TAG, "  Group: %s", this->group_label().c_str());
  ESP_LOGCONFIG(TAG, "  Refresh: %lu ms",
                static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Stale after: %lu ms",
                static_cast<unsigned long>(this->stale_after_ms_));
  ESP_LOGCONFIG(TAG, "  Worker: %s", YESNO(this->worker_task_ != nullptr));
}

bool OutageClient::set_group(int32_t group, int32_t subgroup) {
  if (group < 0 || group > 60 || subgroup < 1 || subgroup > 9) {
    ESP_LOGW(TAG, "Outage group rejected=%ld.%ld", static_cast<long>(group),
             static_cast<long>(subgroup));
    return false;
  }

  const uint8_t value = static_cast<uint8_t>(group);
  const uint8_t subvalue = static_cast<uint8_t>(subgroup);
  bool changed = false;
  {
    // Group selection and snapshot invalidation are one transaction.
    // The worker uses the same lock pair when committing fetched data,
    // so an old-group HTTP result cannot be published after this point.
    std::scoped_lock lock(this->group_mutex_, this->state_mutex_);
    if (value != this->stored_group_.group ||
        subvalue != this->stored_group_.subgroup) {
      this->stored_group_.magic = GROUP_MAGIC;
      this->stored_group_.group = value;
      this->stored_group_.subgroup = subvalue;
      const uint32_t sequence = this->snapshot_.sequence;
      this->snapshot_ = OutageSnapshot{};
      this->snapshot_.sequence = sequence;
      changed = true;
    }
  }

  if (!changed) {
    if (value != 0) this->request_refresh();
    return true;
  }

  this->save_group_preference_();
  if (value != 0) this->request_refresh();
  this->update_pending_.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "Outage group selected=%u.%u", static_cast<unsigned>(value),
           static_cast<unsigned>(subvalue));
  return true;
}

void OutageClient::request_refresh() {
  if (!this->configured()) {
    ESP_LOGI(TAG, "Outage refresh skipped: group is not configured");
    return;
  }
  this->refresh_requested_.store(true, std::memory_order_release);
}

void OutageClient::dump_runtime() const {
  this->dump_calendar_();
  OutageSnapshot copy;
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    copy = this->snapshot_;
  }
  ESP_LOGI(
      TAG,
      "OUTAGE group=%u snapshot_group=%u valid=%s stale=%s fetch_ok=%s "
      "probable=%s seq=%lu",
      static_cast<unsigned>(this->group_number()),
      static_cast<unsigned>(copy.group), YESNO(copy.valid),
      YESNO(this->schedule_stale()), YESNO(copy.fetch_ok),
      YESNO(copy.probable_valid), static_cast<unsigned long>(copy.sequence));
  ESP_LOGI(TAG, "OUTAGE updated=%s today_status=%u tomorrow_status=%u",
           copy.provider_updated.c_str(), static_cast<unsigned>(copy.today.status),
           static_cast<unsigned>(copy.tomorrow.status));
  for (size_t i = 0; i < copy.today.count; ++i) {
    const auto &slot = copy.today.slots[i];
    ESP_LOGI(TAG, "OUTAGE_TODAY index=%u start=%u end=%u type=%u",
             static_cast<unsigned>(i), static_cast<unsigned>(slot.start_min),
             static_cast<unsigned>(slot.end_min), static_cast<unsigned>(slot.type));
  }
}

/** Log raw provider dates and their current calendar validity read-only. */
void OutageClient::dump_calendar_() const {
  if (this->time_source_ == nullptr) {
    ESP_LOGI(TAG, "OUTAGE_CALENDAR clock_valid=NO clock_missing=YES");
    return;
  }
  const auto now = this->time_source_->now();
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  ESP_LOGI(TAG,
      "OUTAGE_CALENDAR clock_valid=%s local_date=%04u-%02u-%02u "
      "today_current=%s tomorrow_current=%s today_date=%s tomorrow_date=%s",
      YESNO(now.is_valid()), static_cast<unsigned>(now.year),
      static_cast<unsigned>(now.month), static_cast<unsigned>(now.day_of_month),
      YESNO(this->day_current_(this->snapshot_.today, false)),
      YESNO(this->day_current_(this->snapshot_.tomorrow, true)),
      this->snapshot_.today.date.c_str(), this->snapshot_.tomorrow.date.c_str());
}

bool OutageClient::qa_force_cached_state() {
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    if (!this->snapshot_.valid) return false;
    this->snapshot_.fetch_ok = false;
  }
  this->update_pending_.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "QA outage cached state forced");
  return true;
}

bool OutageClient::qa_set_age_seconds(uint32_t age_seconds) {
  if (age_seconds > 3600U) return false;
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    if (!this->snapshot_.valid) return false;
    this->snapshot_.received_ms = millis() - age_seconds * 1000U;
  }
  this->update_pending_.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "QA outage age=%lu s",
           static_cast<unsigned long>(age_seconds));
  return true;
}

bool OutageClient::configured() const { return this->selected_group_() != 0; }

uint8_t OutageClient::group_number() const { return this->selected_group_(); }

uint8_t OutageClient::subgroup_number() const {
  return this->selected_subgroup_();
}

std::string OutageClient::group_label() const {
  std::lock_guard<std::mutex> lock(this->group_mutex_);
  if (this->stored_group_.group == 0) return "--";
  char value[12];
  std::snprintf(value, sizeof(value), "%u.%u",
                static_cast<unsigned>(this->stored_group_.group),
                static_cast<unsigned>(this->stored_group_.subgroup));
  return value;
}

/** Compare a day while the caller holds the snapshot mutex. */
bool OutageClient::day_current_(const DaySchedule &day, bool tomorrow) const {
  if (!this->snapshot_.valid || this->time_source_ == nullptr) return false;
  const auto now = this->time_source_->now();
  if (!now.is_valid()) return false;
  const CalendarDate local{uint16_t(now.year), uint8_t(now.month),
                           uint8_t(now.day_of_month)};
  return schedule_date_matches(day.date, local, tomorrow);
}

/** Observe date changes without network calls inside the display accessors. */
void OutageClient::check_calendar_() {
  const uint32_t now_ms = millis();
  if (this->calendar_checked_ &&
      now_ms - this->last_calendar_check_ms_ < 1000U) return;
  this->calendar_checked_ = true;
  this->last_calendar_check_ms_ = now_ms;
  uint32_t date = 0;
  if (this->time_source_ != nullptr) {
    const auto now = this->time_source_->now();
    if (now.is_valid())
      date = uint32_t(now.year) * 10000U + now.month * 100U + now.day_of_month;
  }
  if (date == this->calendar_date_) return;
  this->calendar_date_ = date;
  this->update_pending_.store(true, std::memory_order_release);
  if (date == 0) return;
  bool mismatch = false;
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    mismatch = this->snapshot_.valid &&
               !this->day_current_(this->snapshot_.today, false);
  }
  if (mismatch || (this->last_valid_calendar_date_ != 0 &&
                   this->last_valid_calendar_date_ != date)) {
    this->request_refresh();
  }
  this->last_valid_calendar_date_ = date;
}

bool OutageClient::schedule_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->day_current_(this->snapshot_.today, false);
}

/** Report tomorrow independently so missing data is never shown as zero. */
bool OutageClient::tomorrow_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->day_current_(this->snapshot_.tomorrow, true);
}

bool OutageClient::schedule_stale() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return true;
  return millis() - this->snapshot_.received_ms > this->stale_after_ms_;
}

bool OutageClient::last_fetch_ok() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.fetch_ok;
}

bool OutageClient::probable_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.probable_valid;
}

uint32_t OutageClient::sequence() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.sequence;
}

std::string OutageClient::provider_updated() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.provider_updated;
}

uint8_t OutageClient::today_status() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  return static_cast<uint8_t>(this->snapshot_.today.status);
}

uint8_t OutageClient::tomorrow_status() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.tomorrow, true)) return 0;
  return static_cast<uint8_t>(this->snapshot_.tomorrow.status);
}

std::string OutageClient::today_status_text(uint8_t language) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return status_text_(DayStatus::UNKNOWN, language);
  return status_text_(this->snapshot_.today.status, language);
}

std::string OutageClient::tomorrow_status_text(uint8_t language) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.tomorrow, true)) return status_text_(DayStatus::UNKNOWN, language);
  return status_text_(this->snapshot_.tomorrow.status, language);
}

std::string OutageClient::today_date() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return "";
  return this->snapshot_.today.date;
}

std::string OutageClient::tomorrow_date() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.tomorrow, true)) return "";
  return this->snapshot_.tomorrow.date;
}

size_t OutageClient::today_slot_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  return this->snapshot_.today.count;
}

uint16_t OutageClient::today_slot_start(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  if (index >= this->snapshot_.today.count) return 0;
  return this->snapshot_.today.slots[index].start_min;
}

uint16_t OutageClient::today_slot_end(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  if (index >= this->snapshot_.today.count) return 0;
  return this->snapshot_.today.slots[index].end_min;
}

uint8_t OutageClient::today_slot_type(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  if (index >= this->snapshot_.today.count) return 0;
  return static_cast<uint8_t>(this->snapshot_.today.slots[index].type);
}

uint16_t OutageClient::today_outage_minutes() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return 0;
  return outage_minutes_(this->snapshot_.today);
}

uint16_t OutageClient::tomorrow_outage_minutes() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.tomorrow, true)) return 0;
  return outage_minutes_(this->snapshot_.tomorrow);
}

int16_t OutageClient::next_outage_start(int16_t current_minute) const {
  if (current_minute < 0 || current_minute >= 1440) return -1;
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return -1;
  for (size_t i = 0; i < this->snapshot_.today.count; ++i) {
    const auto &slot = this->snapshot_.today.slots[i];
    if (slot.type == SlotType::DEFINITE && slot.end_min > current_minute) {
      return static_cast<int16_t>(slot.start_min);
    }
  }
  return -1;
}

int16_t OutageClient::next_outage_end(int16_t current_minute) const {
  if (current_minute < 0 || current_minute >= 1440) return -1;
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->day_current_(this->snapshot_.today, false)) return -1;
  for (size_t i = 0; i < this->snapshot_.today.count; ++i) {
    const auto &slot = this->snapshot_.today.slots[i];
    if (slot.type == SlotType::DEFINITE && slot.end_min > current_minute) {
      return static_cast<int16_t>(slot.end_min);
    }
  }
  return -1;
}

uint16_t OutageClient::week_outage_minutes(size_t day) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (day >= WEEK_DAYS) return 0;
  return this->snapshot_.week_outage_minutes[day];
}

void OutageClient::worker_entry(void *argument) {
  auto *client = static_cast<OutageClient *>(argument);
  client->worker_loop();
  client->worker_task_ = nullptr;
  vTaskDelete(nullptr);
}

void OutageClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    const bool manual_pending =
        this->refresh_requested_.load(std::memory_order_acquire);
    const bool due = time_reached(now, this->next_refresh_ms_);
    if ((manual_pending || due) && this->configured() &&
        this->retry_after_gate_.ready(now)) {
      bool manual = false;
      if (manual_pending) {
        manual = this->refresh_requested_.exchange(
            false, std::memory_order_acq_rel);
      }
      this->process_refresh_request(!manual);
    } else if (due && !this->configured()) {
      this->next_refresh_ms_ = now + this->refresh_interval_ms_;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void OutageClient::process_refresh_request(bool scheduled) {
  uint8_t group = 0;
  uint8_t subgroup = 1;
  {
    std::lock_guard<std::mutex> lock(this->group_mutex_);
    group = this->stored_group_.group;
    subgroup = this->stored_group_.subgroup;
  }
  const std::string planned_url = this->planned_url_();
  const std::string probable_url = this->probable_url_();
  esp_http_client_config_t config = {};
  config.url = planned_url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 8000;
  config.user_agent = USER_AGENT;
  config.event_handler = collect_group_object;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.fetch_ok = false;
    this->schedule_retry_();
    this->update_pending_.store(true, std::memory_order_release);
    return;
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Accept-Encoding", "identity");

  std::string object;
  OutageSnapshot next;
  const bool fetched = this->fetch_group_json_(
      client, planned_url, OutageWireSchema::PLANNED, group, subgroup,
      &object);
  const bool planned_ok = fetched && this->parse_planned_(object, &next);
  if (!planned_ok) {
    esp_http_client_cleanup(client);
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.fetch_ok = false;
    this->schedule_retry_();
    this->update_pending_.store(true, std::memory_order_release);
    return;
  }
  object.clear();
  const bool probable_fetch = this->fetch_group_json_(
      client, probable_url, OutageWireSchema::PROBABLE, group, subgroup,
      &object);
  esp_http_client_cleanup(client);
  next.probable_valid = probable_fetch && this->parse_probable_(object, &next);
  next.valid = true;
  next.fetch_ok = true;
  next.group = group;
  next.received_ms = millis();

  bool group_changed = false;
  {
    // Recheck and commit atomically against set_group(). This closes the
    // TOCTOU window between the old selected_group_() check and snapshot write.
    std::scoped_lock lock(this->group_mutex_, this->state_mutex_);
    if (group != this->stored_group_.group ||
        subgroup != this->stored_group_.subgroup) {
      group_changed = true;
    } else {
      next.sequence = this->snapshot_.sequence + 1;
      this->snapshot_ = std::move(next);
    }
  }
  if (group_changed) {
    ESP_LOGI(TAG, "Discarding outage result after group change");
    this->refresh_requested_.store(true, std::memory_order_release);
    return;
  }
  this->schedule_success_();
  this->update_pending_.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "Outage request %s group=%u.%u probable=%s",
           scheduled ? "scheduled" : "manual", static_cast<unsigned>(group),
           static_cast<unsigned>(subgroup), YESNO(this->probable_valid()));
}

bool OutageClient::fetch_group_json_(
    esp_http_client_handle_t client, const std::string &url,
    OutageWireSchema schema, uint8_t group, uint8_t subgroup,
    std::string *object) {
  if (!network::is_connected()) {
    ESP_LOGW(TAG, "Outage request skipped: network is offline");
    return false;
  }
  usp_network::HttpsTransaction network_tx(
      "outage", usp_network::HttpsPriority::NORMAL, 2500);
  if (!network_tx.acquired()) {
    ESP_LOGW(TAG, "Outage request skipped: HTTPS gate busy");
    return false;
  }
  char key[12];
  std::snprintf(key, sizeof(key), "%u.%u", static_cast<unsigned>(group),
                static_cast<unsigned>(subgroup));
  std::array<const char *, 5> path{};
  const size_t path_len = group_object_path(schema, key, &path);
  if (path_len == 0) return false;
  if (client == nullptr || object == nullptr) return false;
  GroupObjectCollector collector{path.data(), path_len};
  GroupHttpContext context{&collector, {}};
  if (esp_http_client_set_url(client, url.c_str()) != ESP_OK ||
      esp_http_client_set_user_data(client, &context) != ESP_OK) {
    return false;
  }
  const esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_set_user_data(client, nullptr);
  if (status == 429) {
    const uint32_t delay = this->retry_after_gate_.apply(
        status, context.retry_after, this->retry_delay_ms_, millis());
    ESP_LOGW(TAG, "Outage rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  if (error != ESP_OK || status != 200 || !collector.found() ||
      collector.overflow()) {
    ESP_LOGW(TAG, "Outage HTTP failed err=%s status=%d found=%s overflow=%s",
             esp_err_to_name(error), status, YESNO(collector.found()),
             YESNO(collector.overflow()));
    return false;
  }
  object->assign(collector.object, collector.length());
  return true;
}

bool OutageClient::parse_planned_(
    const std::string &object, OutageSnapshot *snapshot) {
  auto document = json::parse_json(object);
  if (document.isNull()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (!this->parse_day_(root["today"].as<JsonObjectConst>(), &snapshot->today)) {
    return false;
  }
  if (!this->parse_day_(root["tomorrow"].as<JsonObjectConst>(),
                        &snapshot->tomorrow)) {
    return false;
  }
  snapshot->provider_updated = safe_text(root["updatedOn"]);
  return true;
}

bool OutageClient::parse_probable_(
    const std::string &object, OutageSnapshot *snapshot) {
  auto document = json::parse_json(object);
  if (document.isNull()) return false;
  JsonObjectConst slots = document["slots"].as<JsonObjectConst>();
  if (slots.isNull()) return false;
  this->parse_week_(slots, snapshot);
  return true;
}

bool OutageClient::parse_day_(
    JsonObjectConst object, DaySchedule *schedule) const {
  if (object.isNull() || schedule == nullptr) return false;
  schedule->date = safe_text(object["date"]);
  CalendarDate parsed_date{};
  if (!parse_schedule_date(schedule->date, &parsed_date)) return false;
  schedule->status = parse_day_status_(safe_text(object["status"]));
  if (schedule->status == DayStatus::UNKNOWN) return false;
  JsonArrayConst slots = object["slots"].as<JsonArrayConst>();
  if (slots.isNull() || slots.size() > MAX_DAY_SLOTS) return false;
  schedule->count = 0;
  for (JsonObjectConst item : slots) {
    if (item.isNull() || !item["start"].is<int>() ||
        !item["end"].is<int>()) return false;
    const int start = item["start"].as<int>();
    const int end = item["end"].as<int>();
    const SlotType type = parse_slot_type_(safe_text(item["type"]));
    if (start < 0 || end <= start || end > 1440 ||
        type == SlotType::UNKNOWN) return false;
    auto &slot = schedule->slots[schedule->count++];
    slot.start_min = static_cast<uint16_t>(start);
    slot.end_min = static_cast<uint16_t>(end);
    slot.type = type;
  }
  return true;
}

void OutageClient::parse_week_(
    JsonObjectConst slots, OutageSnapshot *snapshot) const {
  snapshot->week_outage_minutes.fill(0);
  for (size_t day = 0; day < WEEK_DAYS; ++day) {
    char key[4];
    std::snprintf(key, sizeof(key), "%u", static_cast<unsigned>(day));
    JsonArrayConst entries = slots[key].as<JsonArrayConst>();
    uint16_t minutes = 0;
    for (JsonObjectConst item : entries) {
      if (parse_slot_type_(safe_text(item["type"])) != SlotType::DEFINITE) {
        continue;
      }
      const int start = item["start"] | -1;
      const int end = item["end"] | -1;
      if (start >= 0 && end > start && end <= 1440) {
        minutes += static_cast<uint16_t>(end - start);
      }
    }
    snapshot->week_outage_minutes[day] = minutes;
  }
}

OutageClient::SlotType OutageClient::parse_slot_type_(
    const std::string &value) {
  if (value == "NotPlanned") return SlotType::NOT_PLANNED;
  if (value == "Definite") return SlotType::DEFINITE;
  return SlotType::UNKNOWN;
}

OutageClient::DayStatus OutageClient::parse_day_status_(
    const std::string &value) {
  if (value == "WaitingForSchedule") return DayStatus::WAITING;
  if (value == "ScheduleApplies") return DayStatus::SCHEDULE_APPLIES;
  if (value == "EmergencyShutdowns") return DayStatus::EMERGENCY;
  if (value == "NoOutages") return DayStatus::NO_OUTAGES;
  return DayStatus::UNKNOWN;
}

std::string OutageClient::status_text_(DayStatus status, uint8_t language) {
  static constexpr const char *TEXT[5][3] = {
      {"Невідомо", "Неизвестно", "Unknown"},
      {"Очікується графік", "Ожидается график", "Waiting for schedule"},
      {"Графік активний", "График активен", "Schedule active"},
      {"Екстрені відключення", "Экстренные отключения", "Emergency outages"},
      {"Без відключень", "Без отключений", "No outages"},
  };
  const uint8_t lang = language <= 2 ? language : 2;
  return TEXT[static_cast<uint8_t>(status)][lang];
}

uint16_t OutageClient::outage_minutes_(const DaySchedule &schedule) {
  uint16_t total = 0;
  for (size_t index = 0; index < schedule.count; ++index) {
    const auto &slot = schedule.slots[index];
    if (slot.type == SlotType::DEFINITE && slot.end_min > slot.start_min) {
      total += static_cast<uint16_t>(slot.end_min - slot.start_min);
    }
  }
  return total;
}

void OutageClient::load_group_preference_() {
  StoredGroup restored{};
  if (!this->group_pref_.load(&restored) || restored.magic != GROUP_MAGIC ||
      restored.group > 60) {
    restored.magic = GROUP_MAGIC;
    restored.group = 0;
    restored.subgroup = 1;
    ESP_LOGI(TAG, "Outage group preference unset; group=0");
  } else {
    // v1 stored the subgroup byte as reserved zero. Treat that as .1 so
    // existing installations migrate without rewriting the preference key.
    if (restored.subgroup < 1 || restored.subgroup > 9) restored.subgroup = 1;
    ESP_LOGI(TAG, "Outage group restored=%u.%u",
             static_cast<unsigned>(restored.group),
             static_cast<unsigned>(restored.subgroup));
  }
  std::lock_guard<std::mutex> lock(this->group_mutex_);
  this->stored_group_ = restored;
}

bool OutageClient::save_group_preference_() {
  StoredGroup value;
  {
    std::lock_guard<std::mutex> lock(this->group_mutex_);
    value = this->stored_group_;
  }
  // Queue only this preference here. IntervalSyncer persists all pending
  // values every 60 s and on shutdown; forcing global sync from an interactive
  // Web/API callback can flush unrelated blobs and stall the main loop.
  if (!this->group_pref_.save(&value)) {
    ESP_LOGW(TAG, "Outage group preference queue failed");
    return false;
  }
  ESP_LOGI(TAG, "Outage group queued=%u.%u",
           static_cast<unsigned>(value.group),
           static_cast<unsigned>(value.subgroup));
  return true;
}

uint8_t OutageClient::selected_group_() const {
  std::lock_guard<std::mutex> lock(this->group_mutex_);
  return this->stored_group_.group;
}

uint8_t OutageClient::selected_subgroup_() const {
  std::lock_guard<std::mutex> lock(this->group_mutex_);
  return this->stored_group_.subgroup;
}

void OutageClient::clear_snapshot_() {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  const uint32_t sequence = this->snapshot_.sequence;
  this->snapshot_ = OutageSnapshot{};
  this->snapshot_.sequence = sequence;
}

void OutageClient::schedule_retry_() {
  this->next_refresh_ms_ = millis() + this->retry_delay_ms_;
  ESP_LOGI(TAG, "Outage retry in %lu ms",
           static_cast<unsigned long>(this->retry_delay_ms_));
  this->retry_delay_ms_ =
      std::min(this->retry_delay_ms_ * 2U, MAX_RETRY_MS);
}

void OutageClient::schedule_success_() {
  this->retry_delay_ms_ = MIN_RETRY_MS;
  this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
}

void OutageClient::log_snapshot_() const {
  OutageSnapshot copy;
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    copy = this->snapshot_;
  }
  ESP_LOGI(TAG,
           "OUTAGE_SNAPSHOT group=%u valid=%s stale=%s fetch_ok=%s seq=%lu "
           "today_min=%u tomorrow_min=%u probable=%s",
           static_cast<unsigned>(this->group_number()), YESNO(copy.valid),
           YESNO(this->schedule_stale()), YESNO(copy.fetch_ok),
           static_cast<unsigned long>(copy.sequence),
           static_cast<unsigned>(outage_minutes_(copy.today)),
           static_cast<unsigned>(outage_minutes_(copy.tomorrow)),
           YESNO(copy.probable_valid));
}

std::string OutageClient::planned_url_() const { return PLANNED_URL; }

std::string OutageClient::probable_url_() const { return PROBABLE_URL; }

}  // namespace esphome::usp_outage
