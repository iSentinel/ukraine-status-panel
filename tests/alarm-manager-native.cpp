#include "alarm_manager.h"
#include <cassert>
#include <cstdio>
#include <functional>
#include <vector>
#include <type_traits>

using namespace esphome;
static_assert(std::is_class_v<usp_alarm::AlarmManager>);

std::vector<std::string> alarm_test_effects;
std::vector<unsigned> fired;
uint32_t alarm_test_millis = 0;
bool expect_durable_before_ring = true;
namespace esphome {
AlarmTestStorage alarm_test_storage;
ESPPreferences preferences_instance;
ESPPreferences *global_preferences = &preferences_instance;
}
void alarm_test_event(uint8_t index) {
  ::usp_alarm_schedule::Store durable{};
  std::memcpy(&durable, esphome::alarm_test_storage.committed.data(),
              sizeof(durable));
  if (expect_durable_before_ring) {
    assert(durable.slots[index].fired_day == 20261021);
  }
  alarm_test_effects.push_back("RING" + std::to_string(index));
  fired.push_back(index);
}

static void reset_storage() {
  esphome::alarm_test_storage = {};
  alarm_test_effects.clear();
  fired.clear();
  alarm_test_millis = 0;
  expect_durable_before_ring = true;
}

static void persistence_before_notification() {
  reset_storage();
  esphome::time::RealTimeClock clock;
  esphome::usp_alarm::AlarmManager manager;
  manager.set_clock(&clock);
  manager.set_trusted([] { return true; });
  manager.setup();
  assert(!manager.slot(0).enabled && !manager.slot(1).enabled);
  assert(esphome::alarm_test_storage.saves == 0);
  assert(manager.configure(0, "07:00", 127, true));
  assert(manager.configure(1, "07:00", 127, true));
  const unsigned saves = esphome::alarm_test_storage.saves;
  manager.update();
  clock.current.hour = 7; clock.current.minute = 0;
  clock.current.timestamp += 60;
  manager.update();
  assert(fired == std::vector<unsigned>({0, 1}));
  assert(esphome::alarm_test_storage.saves == saves + 1);
  assert(manager.active() == 3);
  assert(manager.notification_time(0) == "07:00");
  manager.dismiss();
  assert(manager.active() == 0);
  assert(esphome::alarm_test_storage.saves == saves + 1);
  for (unsigned index = 0; index < 200; ++index) manager.update();
  assert(esphome::alarm_test_storage.saves == saves + 1);
  esphome::usp_alarm::AlarmManager restored;
  restored.set_clock(&clock);
  restored.set_trusted([] { return true; });
  restored.setup();
  assert(restored.slot(0).enabled && restored.slot(0).fired_day == 20261021);
  restored.update();
  assert(fired.size() == 2);
}

static void rejected_and_stale_configuration() {
  reset_storage();
  esphome::usp_alarm::AlarmManager manager;
  manager.setup();
  assert(manager.configure(0, "08:15", 62, true, 0));
  const unsigned saves = esphome::alarm_test_storage.saves;
  assert(manager.revision(0) == 1);
  assert(!manager.configure(0, "09:30", 65, false, 0));
  assert(manager.error_code() == 2);
  assert(manager.time_text(0) == "08:15" && manager.slot(0).enabled);
  assert(!manager.configure(0, "24:99", 127, true));
  assert(!manager.configure(0, std::string("07:00\0junk", 10), 127, true));
  assert(!manager.configure(0, "07:00", 1, true));
  assert(manager.error_code() == 1);
  assert(!manager.configure(100, "07:00", 127, true));
  assert(esphome::alarm_test_storage.saves == saves);
  assert(manager.configure(0, "08:15", 62, true));
  assert(manager.error_code() == 0);
  assert(esphome::alarm_test_storage.saves == saves);
}

static void failed_configuration_is_not_auto_applied() {
  reset_storage();
  esphome::usp_alarm::AlarmManager manager;
  manager.setup();
  assert(manager.configure(0, "08:15", 127, true));
  esphome::alarm_test_storage.sync_ok = false;
  assert(!manager.configure(0, "09:30", 127, true));
  assert(manager.time_text(0) == "08:15");
  assert(!manager.storage_ok());
  esphome::alarm_test_storage.sync_ok = true;
  assert(manager.configure(0, "08:15", 127, true));
  assert(manager.storage_ok());
  assert(manager.time_text(0) == "08:15");
}

static void failed_commit_notifies_and_retries() {
  reset_storage();
  esphome::time::RealTimeClock clock;
  esphome::usp_alarm::AlarmManager manager;
  manager.set_clock(&clock);
  manager.set_trusted([] { return true; });
  manager.setup();
  assert(manager.configure(0, "07:00", 127, true));
  manager.update();
  esphome::alarm_test_storage.sync_ok = false;
  expect_durable_before_ring = false;
  const size_t effects = alarm_test_effects.size();
  clock.current.hour = 7;
  clock.current.minute = 0;
  clock.current.timestamp += 60;
  manager.update();
  assert(fired == std::vector<unsigned>({0}));
  assert(!manager.storage_ok() && manager.active() == 1);
  assert(alarm_test_effects.size() == effects + 3);
  assert(alarm_test_effects[effects] == "SAVE");
  assert(alarm_test_effects[effects + 1] == "COMMIT");
  assert(alarm_test_effects[effects + 2] == "RING0");
  const unsigned saves = esphome::alarm_test_storage.saves;
  for (unsigned second = 1; second < 30; ++second) {
    alarm_test_millis += 1000;
    manager.update();
  }
  assert(esphome::alarm_test_storage.saves == saves);
  esphome::alarm_test_storage.sync_ok = true;
  alarm_test_millis += 1000;
  manager.update();
  assert(esphome::alarm_test_storage.saves == saves + 1);
  assert(manager.storage_ok());
  assert(fired == std::vector<unsigned>({0}));
  ::usp_alarm_schedule::Store durable{};
  std::memcpy(&durable, esphome::alarm_test_storage.committed.data(),
              sizeof(durable));
  assert(durable.slots[0].fired_day == 20261021);
}

static void active_text_is_latched() {
  reset_storage();
  esphome::time::RealTimeClock clock;
  esphome::usp_alarm::AlarmManager manager;
  manager.set_clock(&clock);
  manager.set_trusted([] { return true; });
  manager.setup();
  assert(manager.configure(0, "07:00", 127, true));
  manager.update();
  clock.current.hour = 7; clock.current.minute = 0;
  clock.current.timestamp += 60;
  manager.update();
  assert(manager.configure(0, "08:15", 127, true));
  assert(manager.active() == 1 && fired.size() == 1);
  assert(manager.time_text(0) == "08:15");
  assert(manager.notification_time(0) == "07:00");
}

int main() {
  active_text_is_latched();
  persistence_before_notification();
  rejected_and_stale_configuration();
  failed_configuration_is_not_auto_applied();
  failed_commit_notifies_and_retries();
  std::puts("ALARM_MANAGER=PASS persist_attempt_before_callback coalesced "
            "guards no_idle_writes failure_notifies bounded_retry "
            "stale_draft_rejected restored");
}
