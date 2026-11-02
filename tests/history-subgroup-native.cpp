#include "history_store.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using esphome::usp_history::HistoryBlob;
using esphome::usp_history::HistoryStore;
using esphome::history_test_preferences;

class Inspectable : public HistoryStore {
 public:
  const HistoryBlob &blob() const { return blob_; }
  HistoryBlob &blob() { return blob_; }
};

/** Bind every store to the same valid, deterministic test clock. */
static void setup(Inspectable &store) {
  static esphome::time::RealTimeClock clock;
  store.set_time(&clock);
  store.setup();
}

static void labels_and_dedup() {
  history_test_preferences = {};
  Inspectable store;
  setup(store);
  assert(store.record_schedule("3.2", 60, 120));
  for (uint8_t language = 0; language < 3; ++language) {
    const std::string prefix = language == 2 ? "G3.2" : "Г3.2";
    const auto text = store.event_summary(0, language);
    const auto compact = store.event_summary_compact(0, language);
    assert(text.rfind("25.10 23:00", 0) == 0);
    assert(compact.rfind("25.10 23:00", 0) == 0);
    assert(text.find(prefix) != std::string::npos);
    assert(compact.find(prefix) != std::string::npos);
    assert(compact.find("01:00/02:00") != std::string::npos);
    assert(text.find("3.1") == std::string::npos);
  }
  assert(!store.record_schedule("3.2", 60, 120));
  assert(store.count() == 1);
  assert(store.record_schedule("3.1", 60, 120));
  assert(store.count() == 2);
  assert(store.event_summary(0, 2).find("G3.1") != std::string::npos);
  assert(store.event_summary(1, 2).find("G3.2") != std::string::npos);
  assert(store.record_schedule(uint8_t(3), 61, 121));
  assert(store.event_summary(0, 2).find("G3.?") != std::string::npos);
  assert(!store.record_schedule(uint8_t(3), 1, 2, 3));
  assert(!store.record_schedule(uint8_t(0), 1, 2, 1));
}

/** Invalid labels must not create an event or schedule a flash write. */
static void invalid_labels() {
  history_test_preferences = {};
  Inspectable store;
  setup(store);
  for (const char *value : {"", "--", "0.1", "1.0", "1.3", "1.", "1.22",
                           "1.2x", "01.1", "-1.2", " 1.2", "256.1",
                           "1.2 ", "1.2\n", "1,2", "1.2."}) {
    assert(!store.record_schedule(value, 60, 120));
  }
  assert(store.count() == 0 && store.total_events() == 0);
  assert(history_test_preferences.syncs == 0);
  for (unsigned group = 1; group <= 255; ++group) {
    for (unsigned subgroup = 1; subgroup <= 2; ++subgroup) {
      const auto label = std::to_string(group) + "." + std::to_string(subgroup);
      assert(store.record_schedule(label, group, subgroup));
      assert(store.event_summary(0, 2).find("G" + label + " ") != std::string::npos);
    }
  }
}

static void persisted_legacy(const char *fixture) {
  history_test_preferences = {};
  std::ifstream stream(fixture, std::ios::binary);
  assert(stream.good());
  history_test_preferences.persisted = std::vector<uint8_t>(
      std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  const auto original = history_test_preferences.persisted;
  assert(original.size() == 404);
  Inspectable legacy;
  setup(legacy);
  assert(legacy.loaded_from_nvs() && legacy.storage_ok());
  assert(legacy.count() == 1 && legacy.total_events() == 1);
  assert(legacy.event_summary(0, 2).find("G3.?") != std::string::npos);
  assert(history_test_preferences.syncs == 0);
  assert(history_test_preferences.persisted == original);
  assert(legacy.record_schedule("3.2", 60, 120));
  assert(legacy.flush());
  Inspectable restored;
  setup(restored);
  assert(restored.loaded_from_nvs() && restored.count() == 2);
  assert(restored.event_summary(0, 2).find("G3.2") != std::string::npos);
  assert(restored.event_summary(1, 2).find("G3.?") != std::string::npos);
  assert(restored.blob().version == 1 && sizeof(restored.blob()) == 404);
}

/** The unchanged ring stays bounded when group/subgroup and durations vary. */
static void ring_and_other_events() {
  history_test_preferences = {};
  Inspectable store;
  setup(store);
  for (unsigned index = 0; index < 10000; ++index) {
    const auto group = uint8_t(index % 6 + 1);
    const auto subgroup = uint8_t(index % 2 + 1);
    assert(store.record_schedule(group, index % 1440, index % 800, subgroup));
  }
  assert(store.count() == 32 && store.total_events() == 10000);
  assert(store.blob().head < 32);
  assert(store.record_energy(31, 3));
  assert(store.event_summary(0, 2).find("#31 Grid: sufficient") != std::string::npos);
  assert(store.record_threat(31, 16, 32));
  assert(store.event_summary(0, 2).find("#31 Threat: air alert") != std::string::npos);
  assert(store.flush());
  Inspectable restored;
  setup(restored);
  assert(restored.loaded_from_nvs() && restored.count() == 32);
  assert(restored.total_events() == 10002);
}


static void date_disambiguation() {
  history_test_preferences = {};
  Inspectable store;
  static esphome::time::RealTimeClock clock;
  store.set_time(&clock);
  clock.value = esphome::ESPTime::from_epoch_local(1792499400U);
  store.setup();
  assert(store.record_energy(31, 3));
  clock.value = esphome::ESPTime::from_epoch_local(1792758600U);
  assert(store.record_energy(31, 4));
  const auto recent = store.event_summary(0, 2);
  const auto older = store.event_summary(1, 2);
  assert(recent.rfind("23.10 15:30", 0) == 0);
  assert(older.rfind("20.10 15:30", 0) == 0);
  assert(recent != older);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  labels_and_dedup();
  date_disambiguation();
  invalid_labels();
  persisted_legacy(argv[1]);
  ring_and_other_events();
  std::cout << "HISTORY_SUBGROUP_NATIVE=PASS actual_complete_module labels3 "
               "groups255x2 legacy_v1_404_preserved roundtrip dedup ring10000 "
               "invalid16 dated_summary compact_schedule no_device\n";
}
