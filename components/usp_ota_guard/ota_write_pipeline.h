#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_flash_backend.h"

namespace esphome::usp_ota_guard {

enum class WritePipelineState : uint8_t {
  IDLE = 0,
  OPEN,
  WRITING,
  FINALIZED,
  ABORTED,
  FAILED,
};

enum class WritePipelineFailure : uint8_t {
  NONE = 0,
  BACKEND_UNAVAILABLE,
  INVALID_ARGUMENT,
  INVALID_STATE,
  OFFSET_MISMATCH,
  SIZE_OVERFLOW,
  SIZE_MISMATCH,
  BACKEND_BEGIN_FAILED,
  BACKEND_WRITE_FAILED,
  BACKEND_FINISH_FAILED,
};

struct WritePipelineSnapshot {
  WritePipelineState state{WritePipelineState::IDLE};
  WritePipelineFailure failure{WritePipelineFailure::NONE};
  size_t expected_bytes{0};
  size_t written_bytes{0};
};

// Compile-only ordering/cleanup adapter for a future verified OTA writer.
//
// 0.115.168 intentionally does not instantiate or wire this class from the
// Web guard. The supplied OtaFlashBackend is itself hard-disabled in .167/.168,
// so even an accidental local construction cannot begin a real OTA write.
class OtaWritePipeline final {
 public:
  explicit OtaWritePipeline(OtaFlashBackend *backend) : backend_(backend) {}

  bool begin(size_t expected_bytes);
  bool write(size_t offset, const uint8_t *data, size_t length);
  bool finish();
  void abort();

  WritePipelineSnapshot snapshot() const;
  static const char *state_name(WritePipelineState state);
  static const char *failure_name(WritePipelineFailure failure);

 private:
  void fail_(WritePipelineFailure failure, bool abort_backend);
  void clear_counters_();

  OtaFlashBackend *backend_{nullptr};
  WritePipelineState state_{WritePipelineState::IDLE};
  WritePipelineFailure failure_{WritePipelineFailure::NONE};
  size_t expected_bytes_{0};
  size_t written_bytes_{0};
};

}  // namespace esphome::usp_ota_guard
