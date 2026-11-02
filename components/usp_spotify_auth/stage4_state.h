#pragma once
#include "device_catalog.h"
#include "queue_policy.h"
#include <cstdint>
namespace usp_spotify_stage4_binding {
struct SharedState {
  usp_spotify_devices::Catalog devices{};
  usp_spotify_queue::Queue queue{};
  uint32_t generation{0};
  uint32_t devices_revision{0};
  uint32_t devices_updated_at{0};
  uint32_t queue_updated_at{0};
  void clear() {
    devices.clear();queue.clear();generation=0;devices_revision=0;
    devices_updated_at=0;queue_updated_at=0;
  }
};
/** Only the worker owns parsing buffers; UI reads SharedState under its mutex. */
struct ReadWorkspace {
  usp_spotify_devices::Catalog devices{};
  usp_spotify_queue::Queue queue{};
};
static_assert(sizeof(ReadWorkspace)<=11000,"Read scratch must stay bounded in PSRAM");
static_assert(sizeof(SharedState)<=11000,"Stage4 shared presentation stays bounded in PSRAM");
}  // namespace usp_spotify_stage4_binding
