#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace esphome::usp_ota_guard {

enum class FlashBackendState : uint8_t {
  DISABLED = 0,
  IDLE,
  OPEN,
  WRITING,
  FINALIZED,
  ABORTED,
  FAILED,
};

struct FlashBackendSnapshot {
  FlashBackendState state{FlashBackendState::DISABLED};
  esp_err_t last_error{ESP_OK};
  size_t expected_bytes{0};
  size_t written_bytes{0};
  bool handle_active{false};
  bool runtime_enabled{false};
};

// Compile-validated ESP-IDF OTA backend foundation.
//
// IMPORTANT: 0.115.167 intentionally has no runtime enable setter, no global
// instance, and no reference from the authenticated Web preflight guard. The
// class exists only to validate the real ESP-IDF transaction API and cleanup
// paths before a later, separately reviewed wiring stage.
class OtaFlashBackend final {
 public:
  OtaFlashBackend() = default;
  ~OtaFlashBackend();

  bool begin(size_t image_size);
  bool write(const uint8_t *data, size_t length);
  bool finish();
  void abort();

  FlashBackendSnapshot snapshot() const;
  static const char *state_name(FlashBackendState state);

 private:
  void reset_transaction_();
  void fail_(esp_err_t error);

  // Hard-disabled in this checkpoint. Deliberately no setter exists.
  bool runtime_enabled_{false};
  FlashBackendState state_{FlashBackendState::DISABLED};
  esp_err_t last_error_{ESP_OK};
  const esp_partition_t *partition_{nullptr};
  esp_ota_handle_t handle_{0};
  bool handle_active_{false};
  size_t expected_bytes_{0};
  size_t written_bytes_{0};
};

}  // namespace esphome::usp_ota_guard
