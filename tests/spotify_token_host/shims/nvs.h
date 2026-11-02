#pragma once
#include "esp_err.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using nvs_handle_t=uint32_t;
constexpr int NVS_READWRITE=1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND=-10,ESP_ERR_NVS_INVALID_LENGTH=-11;
namespace nvs_fixture {
inline std::mutex mutex;
inline std::map<std::string,std::vector<uint8_t>> data;
inline std::map<nvs_handle_t,std::string> handles;
inline nvs_handle_t next_handle=1;
inline unsigned opens=0,reads=0,writes=0,commits=0,closes=0;
inline bool fail_open=false,fail_read=false,fail_write=false,fail_commit=false;
inline void reset() {
  std::lock_guard<std::mutex> lock(mutex);
  data.clear();handles.clear();next_handle=1;opens=reads=writes=commits=closes=0;
  fail_open=fail_read=fail_write=fail_commit=false;
}
}
inline esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *output) {
  std::lock_guard<std::mutex> lock(nvs_fixture::mutex);
  assert(std::strcmp(name,"usp_spotify")==0 && mode==NVS_READWRITE && output);
  ++nvs_fixture::opens;if(nvs_fixture::fail_open)return ESP_FAIL;
  *output=nvs_fixture::next_handle++;nvs_fixture::handles[*output]=name;return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {
  std::lock_guard<std::mutex> lock(nvs_fixture::mutex);
  assert(nvs_fixture::handles.erase(handle)==1);++nvs_fixture::closes;
}
inline std::string nvs_fixture_key(nvs_handle_t handle,const char *key) {
  assert(nvs_fixture::handles.count(handle)==1 && (std::strcmp(key,"session_v1")==0 || std::strcmp(key,"local_key_v1")==0));
  return nvs_fixture::handles.at(handle)+"/"+key;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *output,size_t *bytes) {
  std::lock_guard<std::mutex> lock(nvs_fixture::mutex);
  ++nvs_fixture::reads;if(nvs_fixture::fail_read)return ESP_FAIL;
  const auto found=nvs_fixture::data.find(nvs_fixture_key(handle,key));
  if(found==nvs_fixture::data.end())return ESP_ERR_NVS_NOT_FOUND;
  const auto size=found->second.size();
  if(!output){*bytes=size;return ESP_OK;}
  if(*bytes<size){*bytes=size;return ESP_ERR_NVS_INVALID_LENGTH;}
  std::memcpy(output,found->second.data(),size);*bytes=size;return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *input,size_t bytes) {
  std::lock_guard<std::mutex> lock(nvs_fixture::mutex);
  ++nvs_fixture::writes;if(nvs_fixture::fail_write)return ESP_FAIL;
  const auto *first=static_cast<const uint8_t *>(input);
  nvs_fixture::data[nvs_fixture_key(handle,key)]={first,first+bytes};return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  std::lock_guard<std::mutex> lock(nvs_fixture::mutex);
  assert(nvs_fixture::handles.count(handle)==1);++nvs_fixture::commits;
  return nvs_fixture::fail_commit?ESP_FAIL:ESP_OK;
}
