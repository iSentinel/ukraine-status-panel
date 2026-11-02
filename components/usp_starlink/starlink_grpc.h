#pragma once

#include <cstdint>
#include <string>

namespace esphome::usp_starlink {

enum class RpcOutcome : uint8_t {
  NOT_SENT = 0,
  OK = 1,
  REMOTE_ERROR = 2,
  RESULT_UNKNOWN = 3,
  PROTOCOL_ERROR = 4,
};

struct DishGpsStatus {
  bool parsed{false};
  bool gps_valid{false};
  bool inhibit_gps{false};
  uint32_t gps_sats{0};
  int grpc_status{-1};
};

struct RpcCallResult {
  RpcOutcome outcome{RpcOutcome::NOT_SENT};
  int grpc_status{-1};
  uint32_t latency_ms{0};
  bool dispatched{false};
};

RpcCallResult dish_get_status(const std::string &host, uint16_t port,
                              uint32_t timeout_ms, DishGpsStatus *status);
RpcCallResult dish_set_gps_inhibit(const std::string &host, uint16_t port,
                                   uint32_t timeout_ms, bool inhibit);

}  // namespace esphome::usp_starlink
