#pragma once
#include "device_transfer_plan.h"
#include "value_control_policy.h"
#include "mode_control_policy.h"
#include "queue_policy.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome::usp_spotify_auth {
constexpr size_t STAGE4_DEVICE_BODY=16384;
constexpr size_t STAGE4_QUEUE_BODY=32768;
struct Stage4HttpResult {
  bool attempted{false};int status{0};bool retry_known{false},retry_excessive{false};
  uint32_t retry_seconds{0};bool complete{false};size_t size{0};
};
using TransferCurrent=bool (*)(void *,const usp_spotify_devices::TransferRequest &);
using ValueCurrent=bool (*)(void *,const usp_spotify_value_controls::Request &);
using ModeCurrent=bool (*)(void *,const usp_spotify_modes::Request &);
using QueueCurrent=bool (*)(void *,const usp_spotify_queue::AddRequest &);
class IdfStage4Transport {
 public:
  static bool available();
  Stage4HttpResult devices(std::string_view,char *,size_t);
  Stage4HttpResult queue(std::string_view,char *,size_t);
  Stage4HttpResult transfer(const usp_spotify_devices::TransferRequest &,std::string_view,TransferCurrent,void *);
  Stage4HttpResult value(const usp_spotify_value_controls::Request &,std::string_view,ValueCurrent,void *);
  Stage4HttpResult mode(const usp_spotify_modes::Request &,std::string_view,ModeCurrent,void *);
  Stage4HttpResult queue_add(const usp_spotify_queue::AddRequest &,std::string_view,QueueCurrent,void *);
};
}  // namespace esphome::usp_spotify_auth
