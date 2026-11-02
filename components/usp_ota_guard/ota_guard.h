#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <mutex>
#include <atomic>

#include <mbedtls/sha256.h>

#include "esphome/core/component.h"
#include "esphome/components/web_server_idf/web_server_idf.h"

namespace esphome::usp_ota_guard {

enum class ManifestStatus : uint8_t {
  OK = 0,
  VERIFIER_NOT_READY,
  INVALID_ARGUMENT,
  MANIFEST_TOO_LARGE,
  BAD_SIGNATURE,
  BAD_JSON,
  SCHEMA_MISMATCH,
  PRODUCT_MISMATCH,
  PROFILE_MISMATCH,
  PROJECT_MISMATCH,
  DEVICE_MISMATCH,
  CHIP_MISMATCH,
  FLASH_MISMATCH,
  TRUST_MISMATCH,
  UPDATE_POLICY_MISMATCH,
  SAFETY_MISMATCH,
  DISTRIBUTION_MISMATCH,
  IMAGE_METADATA_MISMATCH,
  VERSION_INVALID,
  DOWNGRADE_DENIED,
};

struct ManifestValidationResult {
  ManifestStatus status{ManifestStatus::INVALID_ARGUMENT};
  std::string target_version;
  std::string image_sha256;
  uint32_t image_size{0};

  bool ok() const { return this->status == ManifestStatus::OK; }
};

enum class ImageStatus : uint8_t {
  IDLE = 0,
  RECEIVING,
  OK,
  INVALID_MANIFEST,
  INVALID_CHUNK,
  SIZE_OVERFLOW,
  SIZE_MISMATCH,
  HEADER_MISMATCH,
  APP_DESC_MISMATCH,
  HASH_FAILURE,
  SHA256_MISMATCH,
};

struct ImagePreflightResult {
  ImageStatus status{ImageStatus::IDLE};
  uint32_t bytes_seen{0};
  std::string sha256;

  bool ok() const { return this->status == ImageStatus::OK; }
};

enum class CandidateState : uint8_t {
  IDLE = 0,
  MANIFEST_ACCEPTED,
  RECEIVING_IMAGE,
  VERIFIED,
  FAILED,
  ABORTED,
};

enum class CandidateFailure : uint8_t {
  NONE = 0,
  NOT_IDLE,
  INVALID_STATE,
  MANIFEST_REJECTED,
  IMAGE_INIT_FAILED,
  CHUNK_REJECTED,
  IMAGE_REJECTED,
  SESSION_EXPIRED,
};

struct CandidateSnapshot {
  CandidateState state{CandidateState::IDLE};
  CandidateFailure failure{CandidateFailure::NONE};
  ManifestStatus manifest_status{ManifestStatus::INVALID_ARGUMENT};
  ImageStatus image_status{ImageStatus::IDLE};
  std::string target_version;
  uint32_t bytes_seen{0};
  uint32_t expected_bytes{0};

  bool verified() const { return this->state == CandidateState::VERIFIED; }
};

enum class WriterState : uint8_t {
  IDLE = 0,
  PREPARED,
  WRITING,
  FINALIZED,
  FAILED,
  ABORTED,
};

enum class WriterFailure : uint8_t {
  NONE = 0,
  INVALID_STATE,
  INVALID_PERMIT,
  EXPECTATION_INVALID,
  HASH_INIT_FAILED,
  INVALID_CHUNK,
  OFFSET_MISMATCH,
  SIZE_OVERFLOW,
  SIZE_MISMATCH,
  HASH_FAILURE,
  SHA256_MISMATCH,
};

struct WriterSnapshot {
  WriterState state{WriterState::IDLE};
  WriterFailure failure{WriterFailure::NONE};
  uint32_t bytes_seen{0};
  uint32_t expected_bytes{0};
  std::string sha256;

  bool finalized() const { return this->state == WriterState::FINALIZED; }
};

class OtaGuard final : public Component, public web_server_idf::AsyncWebHandler {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleBody(web_server_idf::AsyncWebServerRequest *request, uint8_t *data,
                  size_t len, size_t index, size_t total) override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }

  bool ready() const { return this->ready_; }
  bool self_test_ok() const { return this->self_test_ok_; }
  const char *key_id() const;

  bool verify_detached(const uint8_t *message, size_t message_length,
                       const uint8_t *signature, size_t signature_length) const;
  bool version_allowed(const std::string &target, const std::string &current,
                       bool allow_downgrade = false) const;
  ManifestValidationResult validate_signed_manifest(
      const uint8_t *manifest, size_t manifest_length,
      const uint8_t *signature, size_t signature_length,
      const std::string &current_version, bool allow_downgrade = false) const;
  static const char *manifest_status_name(ManifestStatus status);

  bool begin_image_preflight(const ManifestValidationResult &manifest);
  bool add_image_chunk(const uint8_t *data, size_t length);
  ImagePreflightResult finish_image_preflight();
  void reset_image_preflight();
  ImageStatus image_status() const {
    std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
    return this->image_status_;
  }
  uint32_t image_bytes_seen() const {
    std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
    return this->image_bytes_seen_;
  }
  static const char *image_status_name(ImageStatus status);

  bool begin_candidate(const uint8_t *manifest, size_t manifest_length,
                       const uint8_t *signature, size_t signature_length,
                       const std::string &current_version,
                       bool allow_downgrade = false);
  bool add_candidate_chunk(const uint8_t *data, size_t length);
  bool finish_candidate();
  void abort_candidate();
  void reset_candidate();
  CandidateSnapshot candidate_snapshot() const;
  CandidateState candidate_state() const {
    std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
    return this->candidate_state_;
  }
  static const char *candidate_state_name(CandidateState state);
  static const char *candidate_failure_name(CandidateFailure failure);

  bool begin_writer_dry_run(const std::string &permit);
  bool add_writer_dry_run_chunk(uint32_t offset, const uint8_t *data,
                                size_t length);
  bool finish_writer_dry_run();
  void abort_writer_dry_run();
  void reset_writer_dry_run();
  WriterSnapshot writer_snapshot() const;
  static const char *writer_state_name(WriterState state);
  static const char *writer_failure_name(WriterFailure failure);

 protected:
  static bool parse_version_core_(const std::string &value, uint32_t out[3]);
  bool validate_image_prefix_() const;
  void fail_image_(ImageStatus status);
  static bool route_is_(web_server_idf::AsyncWebServerRequest *request,
                        const char *path);
  static bool content_type_is_(web_server_idf::AsyncWebServerRequest *request,
                               const char *expected);
  void send_json_(web_server_idf::AsyncWebServerRequest *request, int code,
                  const std::string &body) const;
  void send_json_with_session_(web_server_idf::AsyncWebServerRequest *request,
                               int code, const std::string &body) const;
  void send_json_with_commit_arm_(
      web_server_idf::AsyncWebServerRequest *request, int code,
      const std::string &body) const;
  void send_json_with_write_permit_(
      web_server_idf::AsyncWebServerRequest *request, int code,
      const std::string &body) const;
  void send_rate_limited_(web_server_idf::AsyncWebServerRequest *request,
                          uint32_t retry_after_ms) const;
  struct RateWindow {
    uint32_t started_ms{0};
    uint16_t count{0};
    bool initialized{false};
  };
  static bool allow_rate_(RateWindow &window, uint16_t limit,
                          uint32_t period_ms, uint32_t now,
                          uint32_t *retry_after_ms);
  bool create_session_token_();
  bool session_token_matches_(web_server_idf::AsyncWebServerRequest *request) const;
  bool session_expired_(uint32_t now) const;
  void expire_session_if_needed_();
  void touch_session_();
  uint32_t session_remaining_ms_(uint32_t now) const;
  uint32_t session_idle_remaining_ms_(uint32_t now) const;
  void clear_session_token_();
  bool create_commit_arm_();
  bool commit_arm_matches_(web_server_idf::AsyncWebServerRequest *request) const;
  bool commit_arm_expired_(uint32_t now) const;
  void expire_commit_arm_if_needed_();
  uint32_t commit_arm_remaining_ms_(uint32_t now) const;
  void clear_commit_arm_();
  bool create_write_permit_();
  bool write_permit_matches_(const std::string &permit) const;
  bool write_permit_expired_(uint32_t now) const;
  void expire_write_permit_if_needed_();
  uint32_t write_permit_remaining_ms_(uint32_t now) const;
  void clear_write_permit_();
  void fail_writer_(WriterFailure failure);
  void handle_manifest_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_image_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_status_request_(web_server_idf::AsyncWebServerRequest *request) const;
  void handle_reset_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_commit_arm_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_commit_check_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_writer_request_(web_server_idf::AsyncWebServerRequest *request);
  void handle_writer_abort_request_(web_server_idf::AsyncWebServerRequest *request);

  // HTTP callbacks and public state APIs share this lock. Recursion is
  // required by their existing nested reset/abort calls. The main loop
  // only tries the lock, so a busy HTTP callback cannot stall the UI.
  mutable std::recursive_mutex state_mutex_;

  std::atomic<bool> sodium_ready_{false};
  std::atomic<bool> self_test_ok_{false};
  std::atomic<bool> ready_{false};

  static constexpr size_t IMAGE_PREFIX_BYTES = 128;
  std::array<uint8_t, IMAGE_PREFIX_BYTES> image_prefix_{};
  size_t image_prefix_length_{0};
  mbedtls_sha256_context image_sha_{};
  bool image_sha_active_{false};
  ImageStatus image_status_{ImageStatus::IDLE};
  uint32_t image_bytes_seen_{0};
  uint32_t expected_image_size_{0};
  std::string expected_image_sha256_;

  CandidateState candidate_state_{CandidateState::IDLE};
  CandidateFailure candidate_failure_{CandidateFailure::NONE};
  ManifestStatus candidate_manifest_status_{ManifestStatus::INVALID_ARGUMENT};
  ImageStatus candidate_image_status_{ImageStatus::IDLE};
  ManifestValidationResult candidate_manifest_{};

  static constexpr size_t SESSION_TOKEN_BYTES = 16;
  static constexpr uint32_t SESSION_MAX_LIFETIME_MS = 10U * 60U * 1000U;
  static constexpr uint32_t SESSION_IDLE_TIMEOUT_MS = 90U * 1000U;
  static constexpr uint16_t MANIFEST_RATE_LIMIT = 5;
  static constexpr uint32_t MANIFEST_RATE_PERIOD_MS = 60U * 1000U;
  static constexpr uint16_t RESET_RATE_LIMIT = 5;
  static constexpr uint32_t RESET_RATE_PERIOD_MS = 30U * 1000U;
  static constexpr uint16_t BAD_SESSION_RATE_LIMIT = 10;
  static constexpr uint32_t BAD_SESSION_RATE_PERIOD_MS = 30U * 1000U;
  static constexpr size_t COMMIT_ARM_TOKEN_BYTES = 16;
  static constexpr uint32_t COMMIT_ARM_LIFETIME_MS = 30U * 1000U;
  static constexpr uint16_t COMMIT_ARM_RATE_LIMIT = 5;
  static constexpr uint32_t COMMIT_ARM_RATE_PERIOD_MS = 30U * 1000U;
  static constexpr uint16_t BAD_COMMIT_ARM_RATE_LIMIT = 10;
  static constexpr uint32_t BAD_COMMIT_ARM_RATE_PERIOD_MS = 30U * 1000U;
  static constexpr size_t WRITE_PERMIT_TOKEN_BYTES = 16;
  static constexpr uint32_t WRITE_PERMIT_LIFETIME_MS = 30U * 1000U;
  static constexpr uint16_t BAD_WRITE_PERMIT_RATE_LIMIT = 10;
  static constexpr uint32_t BAD_WRITE_PERMIT_RATE_PERIOD_MS = 30U * 1000U;
  static constexpr uint16_t WRITE_ABORT_RATE_LIMIT = 5;
  static constexpr uint32_t WRITE_ABORT_RATE_PERIOD_MS = 30U * 1000U;
  std::array<uint8_t, SESSION_TOKEN_BYTES> session_token_{};
  std::string session_token_hex_;
  bool session_token_active_{false};
  uint32_t session_started_ms_{0};
  uint32_t session_last_activity_ms_{0};
  std::array<uint8_t, COMMIT_ARM_TOKEN_BYTES> commit_arm_token_{};
  std::string commit_arm_token_hex_;
  bool commit_arm_active_{false};
  uint32_t commit_arm_started_ms_{0};
  std::array<uint8_t, WRITE_PERMIT_TOKEN_BYTES> write_permit_token_{};
  std::string write_permit_token_hex_;
  bool write_permit_active_{false};
  uint32_t write_permit_started_ms_{0};
  WriterState writer_state_{WriterState::IDLE};
  WriterFailure writer_failure_{WriterFailure::NONE};
  uint32_t writer_bytes_seen_{0};
  uint32_t writer_expected_size_{0};
  std::string writer_expected_sha256_;
  std::string writer_actual_sha256_;
  mbedtls_sha256_context writer_sha_{};
  bool writer_sha_active_{false};
  RateWindow manifest_rate_{};
  RateWindow reset_rate_{};
  RateWindow bad_session_rate_{};
  RateWindow commit_arm_rate_{};
  RateWindow bad_commit_arm_rate_{};
  RateWindow bad_write_permit_rate_{};
  RateWindow write_abort_rate_{};

  std::atomic<bool> preflight_route_registered_{false};
  bool request_body_error_{false};
  bool request_session_error_{false};
  bool request_rate_limited_{false};
  uint32_t request_retry_after_ms_{0};
  bool request_image_final_{false};
  bool request_writer_final_{false};
  bool request_write_permit_error_{false};
  bool request_write_permit_missing_{false};
  uint32_t request_writer_offset_{0};
  std::string request_body_;
};

}  // namespace esphome::usp_ota_guard
