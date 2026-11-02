
#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
namespace esphome {
struct HostPreferences;
extern HostPreferences host_preferences;
class ESPPreferenceObject {
 public:
  ESPPreferenceObject()=default;
  explicit ESPPreferenceObject(uint32_t key):key_(key),valid_(true){}
  template<typename T> bool save(const T *src){return save(reinterpret_cast<const uint8_t*>(src),sizeof(T));}
  template<typename T> bool load(T *dst){return load(reinterpret_cast<uint8_t*>(dst),sizeof(T));}
  bool save(const uint8_t *src,size_t len);
  bool load(uint8_t *dst,size_t len);
 private:
  uint32_t key_{0}; bool valid_{false};
};
struct HostPreferences {
  std::unordered_map<uint32_t,std::vector<uint8_t>> persisted;
  std::unordered_map<uint32_t,std::vector<uint8_t>> pending;
  std::vector<uint32_t> pending_order;
  int fail_sync_after{-1};
  uint32_t make_calls{0};
  uint32_t save_calls{0}; uint32_t sync_calls{0}; bool fail_save{false}; bool fail_sync{false}; int fail_save_after{-1};
  template<typename T> ESPPreferenceObject make_preference(uint32_t key,bool){++make_calls;return ESPPreferenceObject(key);}
  bool load_from_key(uint32_t key, uint8_t *data, size_t size) {
    return ESPPreferenceObject(key).load(data, size);
  }
  bool sync() {
    ++sync_calls;
    unsigned written = 0;
    bool failed = fail_sync;
    for (uint32_t key : pending_order) {
      auto item = pending.find(key);
      if (item == pending.end()) continue;
      if (fail_sync || (fail_sync_after >= 0 &&
                       static_cast<int>(written) >= fail_sync_after)) {
        failed = true;
        continue;
      }
      persisted[key] = item->second;
      ++written;
    }
    pending.clear();
    pending_order.clear();
    return !failed;
  }
};
inline HostPreferences host_preferences;
inline HostPreferences *global_preferences=&host_preferences;
inline bool ESPPreferenceObject::save(const uint8_t *src,size_t len){
  if(!valid_||host_preferences.fail_save) return false;
  if(host_preferences.fail_save_after>=0 &&
     static_cast<int>(host_preferences.save_calls)>=host_preferences.fail_save_after) return false;
  if (host_preferences.pending.empty()) host_preferences.pending_order.clear();
  if (host_preferences.pending.find(key_) == host_preferences.pending.end())
    host_preferences.pending_order.push_back(key_);
  ++host_preferences.save_calls;
  host_preferences.pending[key_]=std::vector<uint8_t>(src,src+len);
  return true;
}
inline bool ESPPreferenceObject::load(uint8_t *dst,size_t len){
  if(!valid_) return false;
  auto p=host_preferences.pending.find(key_);
  if(p!=host_preferences.pending.end()){
    if(p->second.size()!=len) return false;
    std::memcpy(dst,p->second.data(),len);
    return true;
  }
  auto it=host_preferences.persisted.find(key_);
  if(it==host_preferences.persisted.end()||it->second.size()!=len) return false;
  std::memcpy(dst,it->second.data(),len);
  return true;
}
}
