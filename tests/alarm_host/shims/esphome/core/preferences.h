#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
extern std::vector<std::string> alarm_test_effects;
namespace esphome {
struct AlarmTestStorage {
  std::array<uint8_t, 32> pending{}, committed{};
  bool present{false}, save_ok{true}, sync_ok{true};
  unsigned saves{}, commits{};
};
extern AlarmTestStorage alarm_test_storage;
class ESPPreferenceObject {
 public:
  template<typename T> bool load(T *value) {
    if (!alarm_test_storage.present) return false;
    std::memcpy(value, alarm_test_storage.committed.data(), sizeof(T));
    return true;
  }
  template<typename T> bool save(const T *value) {
    ++alarm_test_storage.saves;
    alarm_test_effects.emplace_back("SAVE");
    if (!alarm_test_storage.save_ok) return false;
    std::memcpy(alarm_test_storage.pending.data(), value, sizeof(T));
    return true;
  }
};
class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t, bool) {
    static_assert(sizeof(T) == 32);
    return {};
  }
  bool sync() {
    ++alarm_test_storage.commits;
    alarm_test_effects.emplace_back("COMMIT");
    if (!alarm_test_storage.sync_ok) return false;
    alarm_test_storage.committed = alarm_test_storage.pending;
    alarm_test_storage.present = true;
    return true;
  }
};
extern ESPPreferences *global_preferences;
}
