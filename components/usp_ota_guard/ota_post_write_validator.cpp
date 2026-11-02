#include "ota_post_write_validator.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

namespace esphome::usp_ota_guard {
namespace {
constexpr size_t kReadChunkBytes = 1024;
}

const char *OtaPostWriteValidator::state_name(PostWriteValidationState state) {
  switch (state) {
    case PostWriteValidationState::IDLE: return "idle";
    case PostWriteValidationState::VALIDATING: return "validating";
    case PostWriteValidationState::VALID: return "valid";
    case PostWriteValidationState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaPostWriteValidator::failure_name(
    PostWriteValidationFailure failure) {
  switch (failure) {
    case PostWriteValidationFailure::NONE: return "none";
    case PostWriteValidationFailure::INVALID_ARGUMENT: return "invalid_argument";
    case PostWriteValidationFailure::PARTITION_INVALID: return "partition_invalid";
    case PostWriteValidationFailure::NOT_APP_PARTITION: return "not_app_partition";
    case PostWriteValidationFailure::DESCRIPTION_READ_FAILED: return "description_read_failed";
    case PostWriteValidationFailure::PROJECT_MISMATCH: return "project_mismatch";
    case PostWriteValidationFailure::VERSION_MISMATCH: return "version_mismatch";
    case PostWriteValidationFailure::READ_FAILED: return "read_failed";
    case PostWriteValidationFailure::HASH_FAILED: return "hash_failed";
    case PostWriteValidationFailure::DIGEST_MISMATCH: return "digest_mismatch";
  }
  return "unknown";
}

bool OtaPostWriteValidator::fixed_string_matches_(
    const char *actual, size_t actual_capacity, const char *expected) {
  if (actual == nullptr || expected == nullptr || actual_capacity == 0) return false;
  const size_t actual_len = strnlen(actual, actual_capacity);
  const size_t expected_len = strnlen(expected, actual_capacity + 1);
  if (actual_len == actual_capacity || expected_len == 0 ||
      expected_len > actual_capacity) return false;
  return actual_len == expected_len &&
         std::memcmp(actual, expected, actual_len) == 0;
}

bool OtaPostWriteValidator::constant_time_equal_(
    const uint8_t *left, const uint8_t *right, size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

void OtaPostWriteValidator::fail_(PostWriteValidationFailure failure,
                                  esp_err_t error) {
  this->snapshot_.state = PostWriteValidationState::FAILED;
  this->snapshot_.failure = failure;
  this->snapshot_.last_error = error;
}

esp_err_t OtaPostWriteValidator::hash_partition_(
    const esp_partition_t *partition, size_t image_size, uint8_t digest[32],
    size_t *read_bytes) {
  std::array<uint8_t, kReadChunkBytes> buffer{};
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    return ESP_FAIL;
  }

  size_t offset = 0;
  while (offset < image_size) {
    const size_t length = std::min(buffer.size(), image_size - offset);
    const esp_err_t error = esp_partition_read(partition, offset,
                                                buffer.data(), length);
    if (error != ESP_OK ||
        mbedtls_sha256_update(&context, buffer.data(), length) != 0) {
      mbedtls_sha256_free(&context);
      return error == ESP_OK ? ESP_FAIL : error;
    }
    offset += length;
    *read_bytes = offset;
  }

  const int result = mbedtls_sha256_finish(&context, digest);
  mbedtls_sha256_free(&context);
  return result == 0 ? ESP_OK : ESP_FAIL;
}

bool OtaPostWriteValidator::validate(
    const esp_partition_t *partition, size_t image_size,
    const uint8_t expected_sha256[32], const char *expected_project,
    const char *expected_version) {
  this->snapshot_ = {};
  this->snapshot_.state = PostWriteValidationState::VALIDATING;
  this->snapshot_.expected_bytes = image_size;
  if (partition == nullptr || image_size == 0 || expected_sha256 == nullptr ||
      expected_project == nullptr || expected_version == nullptr) {
    this->fail_(PostWriteValidationFailure::INVALID_ARGUMENT, ESP_ERR_INVALID_ARG);
    return false;
  }

  const esp_partition_t *verified = esp_partition_verify(partition);
  if (verified == nullptr || image_size > partition->size) {
    this->fail_(PostWriteValidationFailure::PARTITION_INVALID, ESP_ERR_NOT_FOUND);
    return false;
  }
  if (verified->type != ESP_PARTITION_TYPE_APP) {
    this->fail_(PostWriteValidationFailure::NOT_APP_PARTITION, ESP_ERR_NOT_SUPPORTED);
    return false;
  }

  esp_app_desc_t descriptor{};
  const esp_err_t desc_error = esp_ota_get_partition_description(verified, &descriptor);
  if (desc_error != ESP_OK || descriptor.magic_word != ESP_APP_DESC_MAGIC_WORD) {
    this->fail_(PostWriteValidationFailure::DESCRIPTION_READ_FAILED,
                desc_error == ESP_OK ? ESP_FAIL : desc_error);
    return false;
  }
  if (!fixed_string_matches_(descriptor.project_name,
                             sizeof(descriptor.project_name), expected_project)) {
    this->fail_(PostWriteValidationFailure::PROJECT_MISMATCH, ESP_FAIL);
    return false;
  }
  if (!fixed_string_matches_(descriptor.version,
                             sizeof(descriptor.version), expected_version)) {
    this->fail_(PostWriteValidationFailure::VERSION_MISMATCH, ESP_FAIL);
    return false;
  }
  this->snapshot_.descriptor_ok = true;

  uint8_t actual_sha256[32]{};
  const esp_err_t hash_error = this->hash_partition_(
      verified, image_size, actual_sha256, &this->snapshot_.read_bytes);
  if (hash_error != ESP_OK) {
    this->fail_(PostWriteValidationFailure::READ_FAILED, hash_error);
    return false;
  }
  if (!constant_time_equal_(actual_sha256, expected_sha256,
                            sizeof(actual_sha256))) {
    this->fail_(PostWriteValidationFailure::DIGEST_MISMATCH, ESP_FAIL);
    return false;
  }

  this->snapshot_.digest_match = true;
  this->snapshot_.failure = PostWriteValidationFailure::NONE;
  this->snapshot_.last_error = ESP_OK;
  this->snapshot_.state = PostWriteValidationState::VALID;
  return true;
}

}  // namespace esphome::usp_ota_guard
