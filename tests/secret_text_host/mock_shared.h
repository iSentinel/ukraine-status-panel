#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGCONFIG(tag, ...) ((void)(tag))
#define LOG_TEXT(...) ((void)0)
#define YESNO(value) ((value) ? "YES" : "NO")

namespace esphome {
namespace setup_priority { constexpr float HARDWARE = 800.0f; }
struct Memory {
  std::map<uint32_t, std::vector<uint8_t>> records;
  unsigned save_calls = 0;
  bool save_success = true;
};
inline Memory memory;
class ESPPreferenceObject {
 public:
  uint32_t key = 0;
  template<typename T> bool load(T *value) {
    auto found = memory.records.find(key);
    if (found == memory.records.end() || found->second.size() != sizeof(T))
      return false;
    std::memcpy(value, found->second.data(), sizeof(T));
    return true;
  }
  template<typename T> bool save(const T *value) {
    ++memory.save_calls;
    if (!memory.save_success) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(value);
    memory.records[key] = std::vector<uint8_t>(bytes, bytes + sizeof(T));
    return true;
  }
};
class Preferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key) {
    static_assert(sizeof(T) == 65, "Legacy format must be exactly 65 bytes");
    ESPPreferenceObject object;
    object.key = key;
    return object;
  }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
class Component {
 public:
  bool warning = false;
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void status_set_warning(const char *) { warning = true; }
  void status_clear_warning() { warning = false; }
};
template<typename T> class Trigger {
 public:
  std::vector<T> values;
  void trigger(const T &value) { values.push_back(value); }
};
inline uint32_t fnv1_hash(const char *value) {
  uint32_t hash = 2166136261U;
  while (*value != '\0') { hash *= 16777619U; hash ^= uint8_t(*value++); }
  return hash;
}
namespace text {
struct Traits {
  unsigned get_min_length() const { return 0; }
  unsigned get_max_length() const { return 64; }
  const char *get_pattern_c_str() const { return ""; }
};
class Text {
 public:
  virtual ~Text() = default;
  Traits traits;
  std::string state;
  std::vector<std::string> published;
  uint32_t get_preference_hash() { return 0x12345678; }
  void publish_state(const std::string &value) {
    state = value;
    published.push_back(value);
  }
 protected:
  virtual void control(const std::string &value) = 0;
};
}  // namespace text
}  // namespace esphome
