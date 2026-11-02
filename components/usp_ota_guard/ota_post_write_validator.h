#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_partition.h>

namespace esphome::usp_ota_guard {

enum class PostWriteValidationState : uint8_t {
  IDLE = 0,
  VALIDATING,
  VALID,
  FAILED,
};

enum class PostWriteValidationFailure : uint8_t {
  NONE = 0,
  INVALID_ARGUMENT,
  PARTITION_INVALID,
  NOT_APP_PARTITION,
  DESCRIPTION_READ_FAILED,
  PROJECT_MISMATCH,
  VERSION_MISMATCH,
  READ_FAILED,
  HASH_FAILED,
  DIGEST_MISMATCH,
};

struct PostWriteValidationSnapshot {
  PostWriteValidationState state{PostWriteValidationState::IDLE};
  PostWriteValidationFailure failure{PostWriteValidationFailure::NONE};
  esp_err_t last_error{ESP_OK};
  size_t expected_bytes{0};
  size_t read_bytes{0};
  bool descriptor_ok{false};
  bool digest_match{false};
};

// Compile-only, read-only validation for a finalized OTA target partition.
//
// 0.115.169 intentionally does not instantiate or wire this class from the
// authenticated Web guard or write pipeline. It can only inspect a partition
// supplied by a future reviewed caller: partition identity, app descriptor,
// exact project/version, and full-image SHA256 readback. It has no flash write,
// erase, boot-partition selection, or restart capability.
class OtaPostWriteValidator final {
 public:
  bool validate(const esp_partition_t *partition, size_t image_size,
                const uint8_t expected_sha256[32],
                const char *expected_project, const char *expected_version);

  PostWriteValidationSnapshot snapshot() const { return this->snapshot_; }
  static const char *state_name(PostWriteValidationState state);
  static const char *failure_name(PostWriteValidationFailure failure);

 private:
  static bool fixed_string_matches_(const char *actual, size_t actual_capacity,
                                    const char *expected);
  static bool constant_time_equal_(const uint8_t *left, const uint8_t *right,
                                   size_t length);
  esp_err_t hash_partition_(const esp_partition_t *partition, size_t image_size,
                            uint8_t digest[32], size_t *read_bytes);
  void fail_(PostWriteValidationFailure failure, esp_err_t error);

  PostWriteValidationSnapshot snapshot_{};
};

}  // namespace esphome::usp_ota_guard
