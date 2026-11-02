#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
namespace esphome {
struct HistoryTestPreferences;
extern HistoryTestPreferences *global_preferences;
class ESPPreferenceObject {
 public:
  template<class T> bool load(T *value);
  template<class T> bool save(const T *value);
};
struct HistoryTestPreferences {
  template<class T> ESPPreferenceObject make_preference(uint32_t value, bool) {
    key = value;
    return {};
  }
  bool sync() {
    ++syncs;
    if (!sync_ok) return false;
    persisted = pending;
    return true;
  }
  uint32_t key{0};
  unsigned syncs{0};
  bool save_ok{true}, sync_ok{true};
  std::vector<uint8_t> pending, persisted;
};
inline HistoryTestPreferences history_test_preferences;
inline HistoryTestPreferences *global_preferences = &history_test_preferences;
template<class T> bool ESPPreferenceObject::load(T *value) {
  const auto &bytes = global_preferences->persisted;
  if (bytes.size() != sizeof(T)) return false;
  std::memcpy(value, bytes.data(), sizeof(T));
  return true;
}
template<class T> bool ESPPreferenceObject::save(const T *value) {
  if (!global_preferences->save_ok) return false;
  const auto *bytes = reinterpret_cast<const uint8_t *>(value);
  global_preferences->pending.assign(bytes, bytes + sizeof(T));
  return true;
}
}
