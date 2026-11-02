#pragma once
#include <cstdint>
namespace esphome::usp_network {
enum class HttpsPriority : uint8_t {
  BACKGROUND = 0,
  NORMAL = 1,
  CRITICAL = 2,
};
struct HttpsTransaction {
  explicit HttpsTransaction(
      const char *, HttpsPriority = HttpsPriority::NORMAL,
      uint32_t = 8000) {}
  bool acquired() const { return true; }
};
}
