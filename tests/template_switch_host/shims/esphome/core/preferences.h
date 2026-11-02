#pragma once
namespace esphome {
inline int preference_reads = 0;
inline int preference_writes = 0;
class ESPPreferenceObject {
 public:
  template<typename T> bool load(T *) { ++preference_reads; return false; }
  template<typename T> bool save(const T *) { ++preference_writes; return true; }
};
}
