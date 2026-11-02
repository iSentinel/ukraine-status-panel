#include "ota_guard.h"
#include "version_policy.h"

#include <array>
#include <cctype>
#include <cstring>
#include <limits>

#include <sodium.h>
#include <sodium/crypto_verify_32.h>
#include <sodium/randombytes.h>
extern "C" {
#include <sodium/private/ed25519_ref10.h>
}
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <esp_random.h>

#include "esphome/components/json/json_util.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome::usp_ota_guard {

static const char *const TAG = "usp.ota.guard";
static constexpr char TRUST_KEY_ID[] = "ed25519:10ac93051fa3c7d5";

static const char *esp_idf_randombytes_name() { return "esp-idf"; }

static uint32_t esp_idf_randombytes_random() { return esp_random(); }

static void esp_idf_randombytes_buf(void *const buf, const size_t size) {
  esp_fill_random(buf, size);
}

static const randombytes_implementation ESP_IDF_RANDOMBYTES_IMPLEMENTATION{
    esp_idf_randombytes_name,
    esp_idf_randombytes_random,
    nullptr,
    nullptr,
    esp_idf_randombytes_buf,
    nullptr,
};

static constexpr size_t MAX_MANIFEST_BYTES = 8192;
static constexpr uint32_t MAX_OTA_IMAGE_BYTES = 0x7C0000;
static constexpr char PRODUCT_NAME[] = "Ukraine Status Panel";
static constexpr char PROFILE_NAME[] = "private-production";
static constexpr char PROJECT_NAME[] = "ukraine-status-panel.panel";
static constexpr char DEVICE_NAME[] = "ukraine-status-panel";
static constexpr char CHIP_FAMILY[] = "ESP32-S3";
static constexpr char FLASH_SIZE[] = "16MB";
static constexpr char FRAMEWORK_VERSION[] = "2026.9.1";
static constexpr char LOCAL_WARNING[] =
    "LOCAL USE ONLY: do not publish or share this OTA image.";
static constexpr char SELF_TEST_MESSAGE[] =
    "Ukraine Status Panel OTA trust self-test v1";
static constexpr std::array<uint8_t, 32> TRUST_PUBLIC_KEY{{
    0xaa, 0x25, 0x5b, 0x03, 0x18, 0x3c, 0x82, 0x76,
    0x61, 0xee, 0x10, 0x97, 0x56, 0xcf, 0xa7, 0xc9,
    0x75, 0x92, 0x7a, 0x3d, 0xf1, 0xd9, 0x08, 0x93,
    0xf5, 0x19, 0x9f, 0xe6, 0xfe, 0x10, 0x3c, 0x12,
}};
static constexpr std::array<uint8_t, 64> SELF_TEST_SIGNATURE{{
    0xf7, 0xd5, 0x32, 0x79, 0xe7, 0x87, 0x80, 0x34,
    0x8c, 0x7d, 0xab, 0x03, 0x46, 0xe9, 0xd1, 0xf4,
    0x8f, 0x25, 0x29, 0xee, 0xc8, 0xb1, 0xa8, 0xc1,
    0x17, 0x83, 0x04, 0x23, 0x4d, 0x3a, 0xb3, 0xef,
    0xcd, 0xec, 0xeb, 0xd6, 0xc7, 0x1e, 0x94, 0xd3,
    0xd5, 0x7b, 0xa1, 0x31, 0xec, 0xe1, 0xac, 0xb5,
    0xe1, 0x90, 0xcd, 0x4b, 0xf5, 0x55, 0x5e, 0x4d,
    0xf3, 0x7f, 0x5b, 0x30, 0xd0, 0x28, 0x86, 0x05,
}};

void OtaGuard::setup() {
  if (randombytes_set_implementation(&ESP_IDF_RANDOMBYTES_IMPLEMENTATION) != 0) {
    ESP_LOGE(TAG, "ESP-IDF libsodium randombytes registration failed");
    this->mark_failed();
    return;
  }
  this->sodium_ready_ = sodium_init() >= 0;
  if (!this->sodium_ready_) {
    ESP_LOGE(TAG, "libsodium initialization failed; verifier disabled");
    this->mark_failed();
    return;
  }
  this->self_test_ok_ = this->verify_detached(
      reinterpret_cast<const uint8_t *>(SELF_TEST_MESSAGE),
      std::strlen(SELF_TEST_MESSAGE), SELF_TEST_SIGNATURE.data(),
      SELF_TEST_SIGNATURE.size());
  this->ready_ = this->self_test_ok_.load();
  if (!this->ready_) {
    ESP_LOGE(TAG, "Ed25519 trust self-test failed; verifier disabled");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "libsodium randombytes backend=%s", randombytes_implementation_name());
  ESP_LOGI(TAG, "Ed25519 verifier ready key_id=%s", TRUST_KEY_ID);
#ifdef USE_WEBSERVER_AUTH
  if (web_server_base::global_web_server_base != nullptr) {
    // IMPORTANT: add_handler() applies WebServerBase authentication middleware.
    // Never register this private engineering surface with add_handler_without_auth().
    web_server_base::global_web_server_base->add_handler(this);
    this->preflight_route_registered_ = true;
    ESP_LOGI(TAG, "Authenticated OTA preflight API registered");
  } else {
    ESP_LOGW(TAG, "WebServerBase unavailable; OTA preflight API disabled");
  }
#else
  ESP_LOGW(TAG, "Web authentication unavailable; OTA preflight API disabled");
#endif
}

void OtaGuard::loop() {
  std::unique_lock<std::recursive_mutex> state_lock(this->state_mutex_, std::try_to_lock);
  if (!state_lock.owns_lock()) return;
  this->expire_session_if_needed_();
  this->expire_commit_arm_if_needed_();
  this->expire_write_permit_if_needed_();
}

void OtaGuard::dump_config() {
  ESP_LOGCONFIG(TAG, "OTA Guard");
  ESP_LOGCONFIG(TAG, "  Key ID: %s", TRUST_KEY_ID);
  ESP_LOGCONFIG(TAG, "  libsodium: %s", YESNO(this->sodium_ready_));
  ESP_LOGCONFIG(TAG, "  Ed25519 self-test: %s", YESNO(this->self_test_ok_));
  ESP_LOGCONFIG(TAG, "  Flash writer: disabled");
  ESP_LOGCONFIG(TAG, "  Writer transport: authenticated dry-run only");
  ESP_LOGCONFIG(TAG, "  Commit arm: one-time interlock");
  ESP_LOGCONFIG(TAG, "  Writer transaction: RAM/hash dry-run only");
  ESP_LOGCONFIG(TAG, "  Stock browser OTA route: disabled");
  ESP_LOGCONFIG(TAG, "  Authenticated preflight API: %s", YESNO(this->preflight_route_registered_));
}

const char *OtaGuard::key_id() const { return TRUST_KEY_ID; }

namespace {

bool exact_string(JsonVariantConst value, const char *expected) {
  const char *text = value.as<const char *>();
  return text != nullptr && std::strcmp(text, expected) == 0;
}

bool exact_bool(JsonVariantConst value, bool expected) {
  return value.is<bool>() && value.as<bool>() == expected;
}

bool valid_sha256(const char *value) {
  if (value == nullptr || std::strlen(value) != 64) return false;
  for (size_t index = 0; index < 64; index++) {
    const char ch = value[index];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace {

bool ed25519_verify_ref10(const uint8_t *signature, const uint8_t *message,
                          size_t message_length, const uint8_t *public_key) {
  if (sc25519_is_canonical(signature + 32) == 0 ||
      ge25519_has_small_order(signature) != 0 ||
      ge25519_is_canonical(public_key) == 0 ||
      ge25519_has_small_order(public_key) != 0) {
    return false;
  }

  ge25519_p3 public_point;
  if (ge25519_frombytes_negate_vartime(&public_point, public_key) != 0) {
    return false;
  }

  uint8_t digest[64]{};
  mbedtls_sha512_context sha;
  mbedtls_sha512_init(&sha);
  bool hash_ok = mbedtls_sha512_starts(&sha, 0) == 0;
  hash_ok = hash_ok && mbedtls_sha512_update(&sha, signature, 32) == 0;
  hash_ok = hash_ok && mbedtls_sha512_update(&sha, public_key, 32) == 0;
  hash_ok = hash_ok &&
            mbedtls_sha512_update(&sha, message, message_length) == 0;
  hash_ok = hash_ok && mbedtls_sha512_finish(&sha, digest) == 0;
  mbedtls_sha512_free(&sha);
  if (!hash_ok) {
    sodium_memzero(digest, sizeof(digest));
    return false;
  }

  sc25519_reduce(digest);
  ge25519_p2 check_point;
  uint8_t check[32]{};
  ge25519_double_scalarmult_vartime(&check_point, digest, &public_point,
                                    signature + 32);
  ge25519_tobytes(check, &check_point);
  const bool valid = crypto_verify_32(check, signature) == 0;
  sodium_memzero(digest, sizeof(digest));
  sodium_memzero(check, sizeof(check));
  return valid;
}

}  // namespace

bool OtaGuard::verify_detached(const uint8_t *message, size_t message_length,
                               const uint8_t *signature,
                               size_t signature_length) const {
  if (!this->sodium_ready_ || message == nullptr || signature == nullptr ||
      signature_length != SELF_TEST_SIGNATURE.size()) {
    return false;
  }
  return ed25519_verify_ref10(signature, message, message_length,
                              TRUST_PUBLIC_KEY.data());
}

bool OtaGuard::parse_version_core_(const std::string &value, uint32_t out[3]) {
  if (out == nullptr) return false;
  esphome::usp_ota_version::Version parsed;
  if (!esphome::usp_ota_version::parse(value, parsed)) return false;
  for (size_t index = 0; index < parsed.core.size(); ++index)
    out[index] = parsed.core[index];
  return true;
}

bool OtaGuard::version_allowed(const std::string &target,
                               const std::string &current,
                               bool allow_downgrade) const {
  return esphome::usp_ota_version::allowed(target, current, allow_downgrade);
}

const char *OtaGuard::manifest_status_name(ManifestStatus status) {
  switch (status) {
    case ManifestStatus::OK: return "ok";
    case ManifestStatus::VERIFIER_NOT_READY: return "verifier_not_ready";
    case ManifestStatus::INVALID_ARGUMENT: return "invalid_argument";
    case ManifestStatus::MANIFEST_TOO_LARGE: return "manifest_too_large";
    case ManifestStatus::BAD_SIGNATURE: return "bad_signature";
    case ManifestStatus::BAD_JSON: return "bad_json";
    case ManifestStatus::SCHEMA_MISMATCH: return "schema_mismatch";
    case ManifestStatus::PRODUCT_MISMATCH: return "product_mismatch";
    case ManifestStatus::PROFILE_MISMATCH: return "profile_mismatch";
    case ManifestStatus::PROJECT_MISMATCH: return "project_mismatch";
    case ManifestStatus::DEVICE_MISMATCH: return "device_mismatch";
    case ManifestStatus::CHIP_MISMATCH: return "chip_mismatch";
    case ManifestStatus::FLASH_MISMATCH: return "flash_mismatch";
    case ManifestStatus::TRUST_MISMATCH: return "trust_mismatch";
    case ManifestStatus::UPDATE_POLICY_MISMATCH: return "update_policy_mismatch";
    case ManifestStatus::SAFETY_MISMATCH: return "safety_mismatch";
    case ManifestStatus::DISTRIBUTION_MISMATCH: return "distribution_mismatch";
    case ManifestStatus::IMAGE_METADATA_MISMATCH: return "image_metadata_mismatch";
    case ManifestStatus::VERSION_INVALID: return "version_invalid";
    case ManifestStatus::DOWNGRADE_DENIED: return "downgrade_denied";
  }
  return "unknown";
}

ManifestValidationResult OtaGuard::validate_signed_manifest(
    const uint8_t *manifest, size_t manifest_length, const uint8_t *signature,
    size_t signature_length, const std::string &current_version,
    bool allow_downgrade) const {
  ManifestValidationResult result;
  if (!this->ready_) {
    result.status = ManifestStatus::VERIFIER_NOT_READY;
    return result;
  }
  if (manifest == nullptr || signature == nullptr || manifest_length == 0 ||
      current_version.empty()) {
    result.status = ManifestStatus::INVALID_ARGUMENT;
    return result;
  }
  if (manifest_length > MAX_MANIFEST_BYTES) {
    result.status = ManifestStatus::MANIFEST_TOO_LARGE;
    return result;
  }

  // Authentication is deliberately first. Do not parse or trust JSON before
  // the exact manifest byte sequence has passed the embedded Ed25519 key.
  if (!this->verify_detached(manifest, manifest_length, signature,
                             signature_length)) {
    result.status = ManifestStatus::BAD_SIGNATURE;
    return result;
  }

  std::string body(reinterpret_cast<const char *>(manifest), manifest_length);
  auto document = json::parse_json(body);
  if (document.isNull()) {
    result.status = ManifestStatus::BAD_JSON;
    return result;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (root.isNull()) {
    result.status = ManifestStatus::BAD_JSON;
    return result;
  }
  if (!root["schema"].is<int>() || root["schema"].as<int>() != 2) {
    result.status = ManifestStatus::SCHEMA_MISMATCH;
    return result;
  }
  if (!exact_string(root["product"], PRODUCT_NAME)) {
    result.status = ManifestStatus::PRODUCT_MISMATCH;
    return result;
  }
  if (!exact_string(root["profile"], PROFILE_NAME)) {
    result.status = ManifestStatus::PROFILE_MISMATCH;
    return result;
  }
  if (!exact_string(root["project"], PROJECT_NAME)) {
    result.status = ManifestStatus::PROJECT_MISMATCH;
    return result;
  }
  if (!exact_string(root["device"], DEVICE_NAME)) {
    result.status = ManifestStatus::DEVICE_MISMATCH;
    return result;
  }
  if (!exact_string(root["chip_family"], CHIP_FAMILY)) {
    result.status = ManifestStatus::CHIP_MISMATCH;
    return result;
  }
  if (!exact_string(root["flash_size"], FLASH_SIZE)) {
    result.status = ManifestStatus::FLASH_MISMATCH;
    return result;
  }

  JsonObjectConst trust = root["trust"].as<JsonObjectConst>();
  if (trust.isNull() || !exact_string(trust["algorithm"], "Ed25519") ||
      !exact_string(trust["key_id"], TRUST_KEY_ID) ||
      !exact_bool(trust["signature_required"], true)) {
    result.status = ManifestStatus::TRUST_MISMATCH;
    return result;
  }

  JsonObjectConst policy = root["update_policy"].as<JsonObjectConst>();
  if (policy.isNull() ||
      !exact_string(policy["downgrade_default"], "deny") ||
      !exact_string(policy["same_version"], "allow") ||
      !exact_bool(policy["same_project_required"], true) ||
      !exact_bool(policy["same_chip_family_required"], true)) {
    result.status = ManifestStatus::UPDATE_POLICY_MISMATCH;
    return result;
  }

  JsonObjectConst safety = root["safety"].as<JsonObjectConst>();
  if (safety.isNull() || !exact_bool(safety["esp_idf_rollback"], true) ||
      !exact_bool(safety["safe_mode"], true) ||
      !exact_bool(safety["browser_ota_enabled"], false) ||
      !exact_bool(safety["signed_image_verification"], true)) {
    result.status = ManifestStatus::SAFETY_MISMATCH;
    return result;
  }

  JsonObjectConst distribution = root["distribution"].as<JsonObjectConst>();
  if (distribution.isNull() || !exact_bool(distribution["public"], false) ||
      !exact_bool(distribution["contains_private_profile"], true) ||
      !exact_string(distribution["warning"], LOCAL_WARNING)) {
    result.status = ManifestStatus::DISTRIBUTION_MISMATCH;
    return result;
  }

  JsonObjectConst image = root["image"].as<JsonObjectConst>();
  JsonObjectConst app_desc = image["esp_app_desc"].as<JsonObjectConst>();
  const char *sha256 = image["sha256"].as<const char *>();
  if (image.isNull() || app_desc.isNull() ||
      !exact_string(image["kind"], "ota") ||
      !exact_string(image["filename"], "firmware.ota.bin") ||
      !image["size"].is<uint32_t>() || image["size"].as<uint32_t>() == 0 ||
      image["size"].as<uint32_t>() > MAX_OTA_IMAGE_BYTES ||
      !valid_sha256(sha256) ||
      !exact_string(app_desc["project_name"], DEVICE_NAME) ||
      !exact_string(app_desc["framework_version"], FRAMEWORK_VERSION)) {
    result.status = ManifestStatus::IMAGE_METADATA_MISMATCH;
    return result;
  }

  const char *target_version = root["version"].as<const char *>();
  uint32_t target_core[3]{};
  uint32_t current_core[3]{};
  if (target_version == nullptr ||
      !this->parse_version_core_(target_version, target_core) ||
      !this->parse_version_core_(current_version, current_core)) {
    result.status = ManifestStatus::VERSION_INVALID;
    return result;
  }
  if (!this->version_allowed(target_version, current_version, allow_downgrade)) {
    result.status = ManifestStatus::DOWNGRADE_DENIED;
    return result;
  }

  result.target_version = target_version;
  result.image_sha256 = sha256;
  result.image_size = image["size"].as<uint32_t>();
  result.status = ManifestStatus::OK;
  return result;
}

namespace {

std::string hex_sha256(const uint8_t digest[32]) {
  static constexpr char HEX[] = "0123456789abcdef";
  std::string output(64, '0');
  for (size_t index = 0; index < 32; index++) {
    output[index * 2] = HEX[digest[index] >> 4];
    output[index * 2 + 1] = HEX[digest[index] & 0x0F];
  }
  return output;
}

uint32_t read_le32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

bool fixed_c_string_equals(const uint8_t *data, size_t width,
                           const char *expected) {
  const size_t expected_length = std::strlen(expected);
  if (expected_length >= width ||
      std::memcmp(data, expected, expected_length) != 0) {
    return false;
  }
  return data[expected_length] == 0;
}

}  // namespace

const char *OtaGuard::image_status_name(ImageStatus status) {
  switch (status) {
    case ImageStatus::IDLE: return "idle";
    case ImageStatus::RECEIVING: return "receiving";
    case ImageStatus::OK: return "ok";
    case ImageStatus::INVALID_MANIFEST: return "invalid_manifest";
    case ImageStatus::INVALID_CHUNK: return "invalid_chunk";
    case ImageStatus::SIZE_OVERFLOW: return "size_overflow";
    case ImageStatus::SIZE_MISMATCH: return "size_mismatch";
    case ImageStatus::HEADER_MISMATCH: return "header_mismatch";
    case ImageStatus::APP_DESC_MISMATCH: return "app_desc_mismatch";
    case ImageStatus::HASH_FAILURE: return "hash_failure";
    case ImageStatus::SHA256_MISMATCH: return "sha256_mismatch";
  }
  return "unknown";
}

void OtaGuard::fail_image_(ImageStatus status) {
  if (this->image_sha_active_) {
    mbedtls_sha256_free(&this->image_sha_);
    this->image_sha_active_ = false;
  }
  this->image_status_ = status;
}

void OtaGuard::reset_image_preflight() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->image_sha_active_) {
    mbedtls_sha256_free(&this->image_sha_);
    this->image_sha_active_ = false;
  }
  this->image_prefix_.fill(0);
  this->image_prefix_length_ = 0;
  this->image_status_ = ImageStatus::IDLE;
  this->image_bytes_seen_ = 0;
  this->expected_image_size_ = 0;
  this->expected_image_sha256_.clear();
}

bool OtaGuard::begin_image_preflight(const ManifestValidationResult &manifest) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  this->reset_image_preflight();
  if (!manifest.ok() || manifest.image_size == 0 ||
      manifest.image_size > MAX_OTA_IMAGE_BYTES ||
      !valid_sha256(manifest.image_sha256.c_str())) {
    this->image_status_ = ImageStatus::INVALID_MANIFEST;
    return false;
  }
  this->expected_image_size_ = manifest.image_size;
  this->expected_image_sha256_ = manifest.image_sha256;
  mbedtls_sha256_init(&this->image_sha_);
  if (mbedtls_sha256_starts(&this->image_sha_, 0) != 0) {
    mbedtls_sha256_free(&this->image_sha_);
    this->image_status_ = ImageStatus::HASH_FAILURE;
    return false;
  }
  this->image_sha_active_ = true;
  this->image_status_ = ImageStatus::RECEIVING;
  return true;
}

bool OtaGuard::add_image_chunk(const uint8_t *data, size_t length) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->image_status_ != ImageStatus::RECEIVING ||
      !this->image_sha_active_ || data == nullptr || length == 0) {
    this->fail_image_(ImageStatus::INVALID_CHUNK);
    return false;
  }
  if (length > this->expected_image_size_ - this->image_bytes_seen_) {
    this->fail_image_(ImageStatus::SIZE_OVERFLOW);
    return false;
  }
  if (mbedtls_sha256_update(&this->image_sha_, data, length) != 0) {
    this->fail_image_(ImageStatus::HASH_FAILURE);
    return false;
  }
  const size_t remaining_prefix = IMAGE_PREFIX_BYTES - this->image_prefix_length_;
  const size_t copy_length = length < remaining_prefix ? length : remaining_prefix;
  if (copy_length > 0) {
    std::memcpy(this->image_prefix_.data() + this->image_prefix_length_, data,
                copy_length);
    this->image_prefix_length_ += copy_length;
  }
  this->image_bytes_seen_ += static_cast<uint32_t>(length);
  return true;
}

bool OtaGuard::validate_image_prefix_() const {
  // ESP image header followed by esp_app_desc inside the first DROM segment.
  // The build contract places the descriptor at image offset 0x20.
  if (this->image_prefix_length_ < 0x70 || this->image_prefix_[0] != 0xE9) {
    return false;
  }
  if (read_le32(this->image_prefix_.data() + 0x20) != 0xABCD5432U) {
    return false;
  }
  if (!fixed_c_string_equals(this->image_prefix_.data() + 0x30, 32,
                             FRAMEWORK_VERSION)) {
    return false;
  }
  return fixed_c_string_equals(this->image_prefix_.data() + 0x50, 32,
                               DEVICE_NAME);
}

ImagePreflightResult OtaGuard::finish_image_preflight() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  ImagePreflightResult result;
  result.bytes_seen = this->image_bytes_seen_;
  if (this->image_status_ != ImageStatus::RECEIVING ||
      !this->image_sha_active_) {
    result.status = this->image_status_;
    return result;
  }
  if (this->image_bytes_seen_ != this->expected_image_size_) {
    this->fail_image_(ImageStatus::SIZE_MISMATCH);
    result.status = this->image_status_;
    return result;
  }
  if (!this->validate_image_prefix_()) {
    const bool basic_header = this->image_prefix_length_ >= 0x24 &&
                              this->image_prefix_[0] == 0xE9 &&
                              read_le32(this->image_prefix_.data() + 0x20) ==
                                  0xABCD5432U;
    this->fail_image_(basic_header ? ImageStatus::APP_DESC_MISMATCH
                                   : ImageStatus::HEADER_MISMATCH);
    result.status = this->image_status_;
    return result;
  }

  uint8_t digest[32]{};
  if (mbedtls_sha256_finish(&this->image_sha_, digest) != 0) {
    sodium_memzero(digest, sizeof(digest));
    this->fail_image_(ImageStatus::HASH_FAILURE);
    result.status = this->image_status_;
    return result;
  }
  mbedtls_sha256_free(&this->image_sha_);
  this->image_sha_active_ = false;
  result.sha256 = hex_sha256(digest);
  sodium_memzero(digest, sizeof(digest));
  if (result.sha256 != this->expected_image_sha256_) {
    this->image_status_ = ImageStatus::SHA256_MISMATCH;
    result.status = this->image_status_;
    return result;
  }
  this->image_status_ = ImageStatus::OK;
  result.status = ImageStatus::OK;
  return result;
}

const char *OtaGuard::candidate_state_name(CandidateState state) {
  switch (state) {
    case CandidateState::IDLE: return "idle";
    case CandidateState::MANIFEST_ACCEPTED: return "manifest_accepted";
    case CandidateState::RECEIVING_IMAGE: return "receiving_image";
    case CandidateState::VERIFIED: return "verified";
    case CandidateState::FAILED: return "failed";
    case CandidateState::ABORTED: return "aborted";
  }
  return "unknown";
}

const char *OtaGuard::candidate_failure_name(CandidateFailure failure) {
  switch (failure) {
    case CandidateFailure::NONE: return "none";
    case CandidateFailure::NOT_IDLE: return "not_idle";
    case CandidateFailure::INVALID_STATE: return "invalid_state";
    case CandidateFailure::MANIFEST_REJECTED: return "manifest_rejected";
    case CandidateFailure::IMAGE_INIT_FAILED: return "image_init_failed";
    case CandidateFailure::CHUNK_REJECTED: return "chunk_rejected";
    case CandidateFailure::IMAGE_REJECTED: return "image_rejected";
    case CandidateFailure::SESSION_EXPIRED: return "session_expired";
  }
  return "unknown";
}

void OtaGuard::reset_candidate() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  this->clear_session_token_();
  this->reset_image_preflight();
  this->candidate_manifest_ = ManifestValidationResult{};
  this->candidate_manifest_status_ = ManifestStatus::INVALID_ARGUMENT;
  this->candidate_image_status_ = ImageStatus::IDLE;
  this->candidate_failure_ = CandidateFailure::NONE;
  this->candidate_state_ = CandidateState::IDLE;
}

void OtaGuard::abort_candidate() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  this->clear_commit_arm_();
  this->clear_write_permit_();
  this->abort_writer_dry_run();
  this->reset_image_preflight();
  this->candidate_manifest_ = ManifestValidationResult{};
  this->candidate_manifest_status_ = ManifestStatus::INVALID_ARGUMENT;
  this->candidate_image_status_ = ImageStatus::IDLE;
  this->candidate_failure_ = CandidateFailure::NONE;
  this->candidate_state_ = CandidateState::ABORTED;
}

bool OtaGuard::begin_candidate(const uint8_t *manifest, size_t manifest_length,
                               const uint8_t *signature,
                               size_t signature_length,
                               const std::string &current_version,
                               bool allow_downgrade) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->candidate_state_ != CandidateState::IDLE) {
    this->candidate_failure_ = CandidateFailure::NOT_IDLE;
    return false;
  }

  this->candidate_manifest_ = this->validate_signed_manifest(
      manifest, manifest_length, signature, signature_length, current_version,
      allow_downgrade);
  this->candidate_manifest_status_ = this->candidate_manifest_.status;
  if (!this->candidate_manifest_.ok()) {
    this->candidate_failure_ = CandidateFailure::MANIFEST_REJECTED;
    this->candidate_state_ = CandidateState::FAILED;
    return false;
  }

  if (!this->begin_image_preflight(this->candidate_manifest_)) {
    this->candidate_image_status_ = this->image_status_;
    this->candidate_failure_ = CandidateFailure::IMAGE_INIT_FAILED;
    this->candidate_state_ = CandidateState::FAILED;
    return false;
  }
  this->candidate_image_status_ = this->image_status_;
  this->candidate_failure_ = CandidateFailure::NONE;
  this->candidate_state_ = CandidateState::MANIFEST_ACCEPTED;
  return true;
}

bool OtaGuard::add_candidate_chunk(const uint8_t *data, size_t length) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->candidate_state_ != CandidateState::MANIFEST_ACCEPTED &&
      this->candidate_state_ != CandidateState::RECEIVING_IMAGE) {
    this->candidate_failure_ = CandidateFailure::INVALID_STATE;
    return false;
  }
  if (!this->add_image_chunk(data, length)) {
    this->candidate_image_status_ = this->image_status_;
    this->candidate_failure_ = CandidateFailure::CHUNK_REJECTED;
    this->candidate_state_ = CandidateState::FAILED;
    return false;
  }
  this->candidate_image_status_ = this->image_status_;
  this->candidate_failure_ = CandidateFailure::NONE;
  this->candidate_state_ = CandidateState::RECEIVING_IMAGE;
  return true;
}

bool OtaGuard::finish_candidate() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->candidate_state_ != CandidateState::MANIFEST_ACCEPTED &&
      this->candidate_state_ != CandidateState::RECEIVING_IMAGE) {
    this->candidate_failure_ = CandidateFailure::INVALID_STATE;
    return false;
  }
  const auto result = this->finish_image_preflight();
  this->candidate_image_status_ = result.status;
  if (!result.ok()) {
    this->candidate_failure_ = CandidateFailure::IMAGE_REJECTED;
    this->candidate_state_ = CandidateState::FAILED;
    return false;
  }
  this->candidate_failure_ = CandidateFailure::NONE;
  this->candidate_state_ = CandidateState::VERIFIED;
  return true;
}

CandidateSnapshot OtaGuard::candidate_snapshot() const {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  CandidateSnapshot snapshot;
  snapshot.state = this->candidate_state_;
  snapshot.failure = this->candidate_failure_;
  snapshot.manifest_status = this->candidate_manifest_status_;
  snapshot.image_status = this->candidate_image_status_;
  snapshot.target_version = this->candidate_manifest_.target_version;
  snapshot.bytes_seen = this->image_bytes_seen_;
  snapshot.expected_bytes = this->candidate_manifest_.image_size;
  return snapshot;
}

const char *OtaGuard::writer_state_name(WriterState state) {
  switch (state) {
    case WriterState::IDLE: return "idle";
    case WriterState::PREPARED: return "prepared";
    case WriterState::WRITING: return "writing";
    case WriterState::FINALIZED: return "finalized";
    case WriterState::FAILED: return "failed";
    case WriterState::ABORTED: return "aborted";
  }
  return "unknown";
}

const char *OtaGuard::writer_failure_name(WriterFailure failure) {
  switch (failure) {
    case WriterFailure::NONE: return "none";
    case WriterFailure::INVALID_STATE: return "invalid_state";
    case WriterFailure::INVALID_PERMIT: return "invalid_permit";
    case WriterFailure::EXPECTATION_INVALID: return "expectation_invalid";
    case WriterFailure::HASH_INIT_FAILED: return "hash_init_failed";
    case WriterFailure::INVALID_CHUNK: return "invalid_chunk";
    case WriterFailure::OFFSET_MISMATCH: return "offset_mismatch";
    case WriterFailure::SIZE_OVERFLOW: return "size_overflow";
    case WriterFailure::SIZE_MISMATCH: return "size_mismatch";
    case WriterFailure::HASH_FAILURE: return "hash_failure";
    case WriterFailure::SHA256_MISMATCH: return "sha256_mismatch";
  }
  return "unknown";
}

void OtaGuard::fail_writer_(WriterFailure failure) {
  if (this->writer_sha_active_) {
    mbedtls_sha256_free(&this->writer_sha_);
    this->writer_sha_active_ = false;
  }
  this->writer_failure_ = failure;
  this->writer_state_ = WriterState::FAILED;
}

void OtaGuard::reset_writer_dry_run() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->writer_sha_active_) {
    mbedtls_sha256_free(&this->writer_sha_);
    this->writer_sha_active_ = false;
  }
  this->writer_state_ = WriterState::IDLE;
  this->writer_failure_ = WriterFailure::NONE;
  this->writer_bytes_seen_ = 0;
  this->writer_expected_size_ = 0;
  this->writer_expected_sha256_.clear();
  this->writer_actual_sha256_.clear();
}

void OtaGuard::abort_writer_dry_run() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->writer_sha_active_) {
    mbedtls_sha256_free(&this->writer_sha_);
    this->writer_sha_active_ = false;
  }
  this->writer_failure_ = WriterFailure::NONE;
  this->writer_state_ = WriterState::ABORTED;
  this->writer_bytes_seen_ = 0;
  this->writer_expected_size_ = 0;
  this->writer_expected_sha256_.clear();
  this->writer_actual_sha256_.clear();
}

bool OtaGuard::begin_writer_dry_run(const std::string &permit) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if (this->candidate_state_ != CandidateState::VERIFIED ||
      !this->session_token_active_) {
    this->fail_writer_(WriterFailure::INVALID_STATE);
    return false;
  }
  this->expire_write_permit_if_needed_();
  if (!this->write_permit_matches_(permit)) {
    this->fail_writer_(WriterFailure::INVALID_PERMIT);
    return false;
  }
  if (this->candidate_manifest_.image_size == 0 ||
      !valid_sha256(this->candidate_manifest_.image_sha256.c_str())) {
    this->fail_writer_(WriterFailure::EXPECTATION_INVALID);
    return false;
  }
  this->clear_write_permit_();
  this->reset_writer_dry_run();
  this->writer_expected_size_ = this->candidate_manifest_.image_size;
  this->writer_expected_sha256_ = this->candidate_manifest_.image_sha256;
  mbedtls_sha256_init(&this->writer_sha_);
  if (mbedtls_sha256_starts(&this->writer_sha_, 0) != 0) {
    mbedtls_sha256_free(&this->writer_sha_);
    this->fail_writer_(WriterFailure::HASH_INIT_FAILED);
    return false;
  }
  this->writer_sha_active_ = true;
  this->writer_state_ = WriterState::PREPARED;
  return true;
}

bool OtaGuard::add_writer_dry_run_chunk(uint32_t offset, const uint8_t *data,
                                        size_t length) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if ((this->writer_state_ != WriterState::PREPARED &&
       this->writer_state_ != WriterState::WRITING) ||
      !this->writer_sha_active_) {
    this->fail_writer_(WriterFailure::INVALID_STATE);
    return false;
  }
  if (data == nullptr || length == 0) {
    this->fail_writer_(WriterFailure::INVALID_CHUNK);
    return false;
  }
  if (offset != this->writer_bytes_seen_) {
    this->fail_writer_(WriterFailure::OFFSET_MISMATCH);
    return false;
  }
  if (length > this->writer_expected_size_ - this->writer_bytes_seen_) {
    this->fail_writer_(WriterFailure::SIZE_OVERFLOW);
    return false;
  }
  if (mbedtls_sha256_update(&this->writer_sha_, data, length) != 0) {
    this->fail_writer_(WriterFailure::HASH_FAILURE);
    return false;
  }
  this->writer_bytes_seen_ += static_cast<uint32_t>(length);
  this->writer_state_ = WriterState::WRITING;
  return true;
}

bool OtaGuard::finish_writer_dry_run() {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  if ((this->writer_state_ != WriterState::PREPARED &&
       this->writer_state_ != WriterState::WRITING) ||
      !this->writer_sha_active_) {
    this->fail_writer_(WriterFailure::INVALID_STATE);
    return false;
  }
  if (this->writer_bytes_seen_ != this->writer_expected_size_) {
    this->fail_writer_(WriterFailure::SIZE_MISMATCH);
    return false;
  }
  uint8_t digest[32]{};
  if (mbedtls_sha256_finish(&this->writer_sha_, digest) != 0) {
    sodium_memzero(digest, sizeof(digest));
    this->fail_writer_(WriterFailure::HASH_FAILURE);
    return false;
  }
  mbedtls_sha256_free(&this->writer_sha_);
  this->writer_sha_active_ = false;
  this->writer_actual_sha256_ = hex_sha256(digest);
  sodium_memzero(digest, sizeof(digest));
  if (this->writer_actual_sha256_ != this->writer_expected_sha256_) {
    this->writer_failure_ = WriterFailure::SHA256_MISMATCH;
    this->writer_state_ = WriterState::FAILED;
    return false;
  }
  this->writer_failure_ = WriterFailure::NONE;
  this->writer_state_ = WriterState::FINALIZED;
  return true;
}

WriterSnapshot OtaGuard::writer_snapshot() const {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  WriterSnapshot snapshot;
  snapshot.state = this->writer_state_;
  snapshot.failure = this->writer_failure_;
  snapshot.bytes_seen = this->writer_bytes_seen_;
  snapshot.expected_bytes = this->writer_expected_size_;
  snapshot.sha256 = this->writer_actual_sha256_;
  return snapshot;
}

namespace {

static constexpr char PREFLIGHT_STATUS_PATH[] = "/usp/ota/preflight/status";
static constexpr char PREFLIGHT_MANIFEST_PATH[] = "/usp/ota/preflight/manifest";
static constexpr char PREFLIGHT_IMAGE_PATH[] = "/usp/ota/preflight/image";
static constexpr char PREFLIGHT_RESET_PATH[] = "/usp/ota/preflight/reset";
static constexpr char PREFLIGHT_COMMIT_ARM_PATH[] = "/usp/ota/preflight/arm";
static constexpr char PREFLIGHT_COMMIT_CHECK_PATH[] = "/usp/ota/preflight/commit-check";
static constexpr char PREFLIGHT_WRITER_PATH[] = "/usp/ota/preflight/write";
static constexpr char PREFLIGHT_WRITER_ABORT_PATH[] = "/usp/ota/preflight/write-abort";
static constexpr char MANIFEST_CONTENT_TYPE[] = "application/vnd.usp.ota-manifest+json";
static constexpr char IMAGE_CONTENT_TYPE[] = "application/octet-stream";
static constexpr char SIGNATURE_HEADER[] = "X-USP-OTA-Signature";
static constexpr char KEY_ID_HEADER[] = "X-USP-OTA-Key-Id";
static constexpr size_t ED25519_SIGNATURE_B64_LENGTH = 88;
static constexpr size_t MAX_WEB_IMAGE_CHUNK_BYTES = 16 * 1024;
static constexpr char IMAGE_OFFSET_HEADER[] = "X-USP-OTA-Offset";
static constexpr char IMAGE_FINAL_HEADER[] = "X-USP-OTA-Final";
static constexpr char SESSION_HEADER[] = "X-USP-OTA-Session";
static constexpr char COMMIT_ARM_HEADER[] = "X-USP-OTA-Commit-Arm";
static constexpr char WRITE_PERMIT_HEADER[] = "X-USP-OTA-Write-Permit";
static constexpr char HEX_DIGITS[] = "0123456789abcdef";

std::string candidate_json(const CandidateSnapshot &snapshot) {
  char buffer[512];
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"state\":\"%s\",\"failure\":\"%s\","
      "\"manifest_status\":\"%s\",\"image_status\":\"%s\","
      "\"target_version\":\"%s\",\"bytes_seen\":%" PRIu32
      ",\"expected_bytes\":%" PRIu32 ",\"writes_flash\":false}",
      OtaGuard::candidate_state_name(snapshot.state),
      OtaGuard::candidate_failure_name(snapshot.failure),
      OtaGuard::manifest_status_name(snapshot.manifest_status),
      OtaGuard::image_status_name(snapshot.image_status),
      snapshot.target_version.c_str(), snapshot.bytes_seen,
      snapshot.expected_bytes);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(buffer)) {
    return "{\"state\":\"failed\",\"error\":\"response_overflow\",\"writes_flash\":false}";
  }
  return std::string(buffer, static_cast<size_t>(written));
}

std::string writer_json(const WriterSnapshot &snapshot) {
  char buffer[512];
  const bool retry_requires_abort = snapshot.state == WriterState::FAILED;
  const bool retry_requires_fresh_permit = snapshot.state == WriterState::ABORTED;
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"writer_state\":\"%s\",\"writer_failure\":\"%s\","
      "\"writer_bytes_seen\":%" PRIu32 ",\"writer_expected_bytes\":%" PRIu32
      ",\"writer_sha256\":\"%s\",\"retry_requires_abort\":%s,"
      "\"retry_requires_fresh_permit\":%s,\"writes_flash\":false}",
      OtaGuard::writer_state_name(snapshot.state),
      OtaGuard::writer_failure_name(snapshot.failure), snapshot.bytes_seen,
      snapshot.expected_bytes, snapshot.sha256.c_str(),
      retry_requires_abort ? "true" : "false",
      retry_requires_fresh_permit ? "true" : "false");
  if (written < 0 || static_cast<size_t>(written) >= sizeof(buffer)) {
    return "{\"writer_state\":\"failed\",\"error\":\"response_overflow\",\"writes_flash\":false}";
  }
  return std::string(buffer, static_cast<size_t>(written));
}

bool parse_decimal_u32(const std::string &value, uint32_t *out) {
  if (out == nullptr || value.empty() || value.size() > 10) return false;
  uint64_t parsed = 0;
  for (const char ch : value) {
    if (ch < '0' || ch > '9') return false;
    parsed = parsed * 10U + static_cast<uint32_t>(ch - '0');
    if (parsed > UINT32_MAX) return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

}  // namespace

bool OtaGuard::route_is_(web_server_idf::AsyncWebServerRequest *request,
                         const char *path) {
  char buffer[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buffer) == path;
}

bool OtaGuard::content_type_is_(web_server_idf::AsyncWebServerRequest *request,
                                const char *expected) {
  const auto content_type = request->get_header("Content-Type");
  return content_type.has_value() && content_type.value() == expected;
}

void OtaGuard::send_json_(web_server_idf::AsyncWebServerRequest *request,
                          int code, const std::string &body) const {
  request->send(code, "application/json", body.c_str());
}

void OtaGuard::send_json_with_session_(
    web_server_idf::AsyncWebServerRequest *request, int code,
    const std::string &body) const {
  auto *response = request->beginResponse(code, "application/json", body);
  if (this->session_token_active_)
    response->addHeader(SESSION_HEADER, this->session_token_hex_.c_str());
  request->send(response);
}

void OtaGuard::send_json_with_commit_arm_(
    web_server_idf::AsyncWebServerRequest *request, int code,
    const std::string &body) const {
  auto *response = request->beginResponse(code, "application/json", body);
  if (this->commit_arm_active_)
    response->addHeader(COMMIT_ARM_HEADER, this->commit_arm_token_hex_.c_str());
  request->send(response);
}

void OtaGuard::send_json_with_write_permit_(
    web_server_idf::AsyncWebServerRequest *request, int code,
    const std::string &body) const {
  auto *response = request->beginResponse(code, "application/json", body);
  if (this->write_permit_active_)
    response->addHeader(WRITE_PERMIT_HEADER,
                        this->write_permit_token_hex_.c_str());
  request->send(response);
}

void OtaGuard::send_rate_limited_(
    web_server_idf::AsyncWebServerRequest *request,
    uint32_t retry_after_ms) const {
  const uint32_t retry_seconds = (retry_after_ms + 999U) / 1000U;
  char body[128];
  const int written = snprintf(
      body, sizeof(body),
      "{\"ok\":false,\"error\":\"rate_limited\",\"retry_after_ms\":%" PRIu32
      ",\"writes_flash\":false}", retry_after_ms);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
    this->send_json_(request, 429,
                     "{\"ok\":false,\"error\":\"rate_limited\",\"writes_flash\":false}");
    return;
  }
  auto *response = request->beginResponse(429, "application/json", body);
  char retry_header[16];
  snprintf(retry_header, sizeof(retry_header), "%" PRIu32, retry_seconds);
  response->addHeader("Retry-After", retry_header);
  request->send(response);
}

bool OtaGuard::allow_rate_(RateWindow &window, uint16_t limit,
                           uint32_t period_ms, uint32_t now,
                           uint32_t *retry_after_ms) {
  if (!window.initialized || now - window.started_ms >= period_ms) {
    window.started_ms = now;
    window.count = 0;
    window.initialized = true;
  }
  const uint32_t elapsed = now - window.started_ms;
  if (window.count >= limit) {
    if (retry_after_ms != nullptr)
      *retry_after_ms = elapsed >= period_ms ? 0 : period_ms - elapsed;
    return false;
  }
  ++window.count;
  if (retry_after_ms != nullptr) *retry_after_ms = 0;
  return true;
}

bool OtaGuard::create_session_token_() {
  this->clear_session_token_();
  esp_fill_random(this->session_token_.data(), this->session_token_.size());
  this->session_token_hex_.resize(this->session_token_.size() * 2U);
  for (size_t i = 0; i < this->session_token_.size(); ++i) {
    this->session_token_hex_[i * 2U] = HEX_DIGITS[this->session_token_[i] >> 4U];
    this->session_token_hex_[i * 2U + 1U] = HEX_DIGITS[this->session_token_[i] & 0x0FU];
  }
  this->session_token_active_ = true;
  this->session_started_ms_ = millis();
  this->session_last_activity_ms_ = this->session_started_ms_;
  return true;
}

bool OtaGuard::session_token_matches_(
    web_server_idf::AsyncWebServerRequest *request) const {
  if (!this->session_token_active_) return false;
  const auto supplied = request->get_header(SESSION_HEADER);
  if (!supplied.has_value() || supplied.value().size() != this->session_token_hex_.size())
    return false;
  return sodium_memcmp(supplied.value().data(), this->session_token_hex_.data(),
                       this->session_token_hex_.size()) == 0;
}


bool OtaGuard::session_expired_(uint32_t now) const {
  if (!this->session_token_active_) return false;
  if (now - this->session_started_ms_ >= SESSION_MAX_LIFETIME_MS) return true;
  const bool stream_active =
      this->candidate_state_ == CandidateState::RECEIVING_IMAGE ||
      this->writer_state_ == WriterState::PREPARED ||
      this->writer_state_ == WriterState::WRITING;
  return stream_active &&
         now - this->session_last_activity_ms_ >= SESSION_IDLE_TIMEOUT_MS;
}

void OtaGuard::expire_session_if_needed_() {
  if (!this->session_expired_(millis())) return;
  ESP_LOGW(TAG, "OTA preflight candidate session expired");
  this->abort_candidate();
  this->clear_session_token_();
  this->candidate_failure_ = CandidateFailure::SESSION_EXPIRED;
  this->request_body_error_ = false;
  this->request_session_error_ = false;
  this->request_rate_limited_ = false;
  this->request_retry_after_ms_ = 0;
  this->request_image_final_ = false;
  this->request_writer_final_ = false;
  this->request_write_permit_error_ = false;
  this->request_write_permit_missing_ = false;
  this->request_writer_offset_ = 0;
}

void OtaGuard::touch_session_() {
  if (this->session_token_active_) this->session_last_activity_ms_ = millis();
}

uint32_t OtaGuard::session_remaining_ms_(uint32_t now) const {
  if (!this->session_token_active_) return 0;
  const uint32_t elapsed = now - this->session_started_ms_;
  return elapsed >= SESSION_MAX_LIFETIME_MS ? 0 : SESSION_MAX_LIFETIME_MS - elapsed;
}

uint32_t OtaGuard::session_idle_remaining_ms_(uint32_t now) const {
  const bool stream_active =
      this->candidate_state_ == CandidateState::RECEIVING_IMAGE ||
      this->writer_state_ == WriterState::PREPARED ||
      this->writer_state_ == WriterState::WRITING;
  if (!this->session_token_active_ || !stream_active) return 0;
  const uint32_t elapsed = now - this->session_last_activity_ms_;
  return elapsed >= SESSION_IDLE_TIMEOUT_MS ? 0 : SESSION_IDLE_TIMEOUT_MS - elapsed;
}

void OtaGuard::clear_session_token_() {
  this->clear_commit_arm_();
  this->clear_write_permit_();
  this->reset_writer_dry_run();
  sodium_memzero(this->session_token_.data(), this->session_token_.size());
  if (!this->session_token_hex_.empty())
    sodium_memzero(this->session_token_hex_.data(), this->session_token_hex_.size());
  this->session_token_hex_.clear();
  this->session_token_active_ = false;
  this->session_started_ms_ = 0;
  this->session_last_activity_ms_ = 0;
}

bool OtaGuard::create_commit_arm_() {
  this->expire_commit_arm_if_needed_();
  if (!this->session_token_active_ ||
      this->candidate_state_ != CandidateState::VERIFIED)
    return false;
  this->clear_commit_arm_();
  esp_fill_random(this->commit_arm_token_.data(), this->commit_arm_token_.size());
  this->commit_arm_token_hex_.resize(this->commit_arm_token_.size() * 2U);
  for (size_t i = 0; i < this->commit_arm_token_.size(); ++i) {
    this->commit_arm_token_hex_[i * 2U] = HEX_DIGITS[this->commit_arm_token_[i] >> 4U];
    this->commit_arm_token_hex_[i * 2U + 1U] =
        HEX_DIGITS[this->commit_arm_token_[i] & 0x0FU];
  }
  this->commit_arm_active_ = true;
  this->commit_arm_started_ms_ = millis();
  return true;
}

bool OtaGuard::commit_arm_matches_(
    web_server_idf::AsyncWebServerRequest *request) const {
  if (!this->commit_arm_active_) return false;
  const auto supplied = request->get_header(COMMIT_ARM_HEADER);
  if (!supplied.has_value() || supplied.value().size() != this->commit_arm_token_hex_.size())
    return false;
  return sodium_memcmp(supplied.value().data(), this->commit_arm_token_hex_.data(),
                       this->commit_arm_token_hex_.size()) == 0;
}

bool OtaGuard::commit_arm_expired_(uint32_t now) const {
  return this->commit_arm_active_ &&
         now - this->commit_arm_started_ms_ >= COMMIT_ARM_LIFETIME_MS;
}

void OtaGuard::expire_commit_arm_if_needed_() {
  if (!this->commit_arm_expired_(millis())) return;
  ESP_LOGW(TAG, "OTA commit arm expired");
  this->clear_commit_arm_();
}

uint32_t OtaGuard::commit_arm_remaining_ms_(uint32_t now) const {
  if (!this->commit_arm_active_) return 0;
  const uint32_t elapsed = now - this->commit_arm_started_ms_;
  return elapsed >= COMMIT_ARM_LIFETIME_MS ? 0 : COMMIT_ARM_LIFETIME_MS - elapsed;
}

void OtaGuard::clear_commit_arm_() {
  sodium_memzero(this->commit_arm_token_.data(), this->commit_arm_token_.size());
  if (!this->commit_arm_token_hex_.empty())
    sodium_memzero(this->commit_arm_token_hex_.data(), this->commit_arm_token_hex_.size());
  this->commit_arm_token_hex_.clear();
  this->commit_arm_active_ = false;
  this->commit_arm_started_ms_ = 0;
}

bool OtaGuard::create_write_permit_() {
  this->expire_write_permit_if_needed_();
  if (!this->session_token_active_ ||
      this->candidate_state_ != CandidateState::VERIFIED)
    return false;
  this->clear_write_permit_();
  this->reset_writer_dry_run();
  esp_fill_random(this->write_permit_token_.data(),
                  this->write_permit_token_.size());
  this->write_permit_token_hex_.resize(this->write_permit_token_.size() * 2U);
  for (size_t i = 0; i < this->write_permit_token_.size(); ++i) {
    this->write_permit_token_hex_[i * 2U] =
        HEX_DIGITS[this->write_permit_token_[i] >> 4U];
    this->write_permit_token_hex_[i * 2U + 1U] =
        HEX_DIGITS[this->write_permit_token_[i] & 0x0FU];
  }
  this->write_permit_active_ = true;
  this->write_permit_started_ms_ = millis();
  return true;
}

bool OtaGuard::write_permit_matches_(const std::string &permit) const {
  if (!this->write_permit_active_ ||
      permit.size() != this->write_permit_token_hex_.size())
    return false;
  return sodium_memcmp(permit.data(), this->write_permit_token_hex_.data(),
                       this->write_permit_token_hex_.size()) == 0;
}

bool OtaGuard::write_permit_expired_(uint32_t now) const {
  return this->write_permit_active_ &&
         now - this->write_permit_started_ms_ >= WRITE_PERMIT_LIFETIME_MS;
}

void OtaGuard::expire_write_permit_if_needed_() {
  if (!this->write_permit_expired_(millis())) return;
  ESP_LOGW(TAG, "OTA dry-run write permit expired");
  this->clear_write_permit_();
}

uint32_t OtaGuard::write_permit_remaining_ms_(uint32_t now) const {
  if (!this->write_permit_active_) return 0;
  const uint32_t elapsed = now - this->write_permit_started_ms_;
  return elapsed >= WRITE_PERMIT_LIFETIME_MS
             ? 0
             : WRITE_PERMIT_LIFETIME_MS - elapsed;
}

void OtaGuard::clear_write_permit_() {
  sodium_memzero(this->write_permit_token_.data(),
                 this->write_permit_token_.size());
  if (!this->write_permit_token_hex_.empty())
    sodium_memzero(this->write_permit_token_hex_.data(),
                   this->write_permit_token_hex_.size());
  this->write_permit_token_hex_.clear();
  this->write_permit_active_ = false;
  this->write_permit_started_ms_ = 0;
}

bool OtaGuard::canHandle(web_server_idf::AsyncWebServerRequest *request) const {
  if (!this->preflight_route_registered_) return false;
  if (request->method() == HTTP_GET) {
    return route_is_(request, PREFLIGHT_STATUS_PATH);
  }
  if (request->method() != HTTP_POST) return false;
  if (route_is_(request, PREFLIGHT_RESET_PATH) ||
      route_is_(request, PREFLIGHT_COMMIT_ARM_PATH) ||
      route_is_(request, PREFLIGHT_COMMIT_CHECK_PATH) ||
      route_is_(request, PREFLIGHT_WRITER_ABORT_PATH)) {
    return request->contentLength() == 0;
  }
  if (route_is_(request, PREFLIGHT_MANIFEST_PATH)) {
    return request->contentLength() > 0 &&
           request->contentLength() <= MAX_MANIFEST_BYTES &&
           content_type_is_(request, MANIFEST_CONTENT_TYPE);
  }
  if (route_is_(request, PREFLIGHT_WRITER_PATH)) {
    if (request->contentLength() == 0 ||
        request->contentLength() > MAX_WEB_IMAGE_CHUNK_BYTES ||
        !content_type_is_(request, IMAGE_CONTENT_TYPE)) {
      return false;
    }
    return request->hasHeader(IMAGE_OFFSET_HEADER) &&
           request->hasHeader(IMAGE_FINAL_HEADER);
  }
  if (!route_is_(request, PREFLIGHT_IMAGE_PATH)) return false;
  if (request->contentLength() == 0 ||
      request->contentLength() > MAX_WEB_IMAGE_CHUNK_BYTES ||
      !content_type_is_(request, IMAGE_CONTENT_TYPE)) {
    return false;
  }
  return request->hasHeader(IMAGE_OFFSET_HEADER) &&
         request->hasHeader(IMAGE_FINAL_HEADER);
}

void OtaGuard::handleBody(web_server_idf::AsyncWebServerRequest *request,
                          uint8_t *data, size_t len, size_t index,
                          size_t total) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  this->expire_session_if_needed_();
  if (route_is_(request, PREFLIGHT_MANIFEST_PATH)) {
    if (index == 0) {
      this->request_body_.clear();
      this->request_body_error_ =
          total == 0 || total > MAX_MANIFEST_BYTES ||
          !content_type_is_(request, MANIFEST_CONTENT_TYPE);
      if (!this->request_body_error_) this->request_body_.reserve(total);
    }
    if (this->request_body_error_) return;
    if (index != this->request_body_.size() || len > total - index ||
        this->request_body_.size() + len > MAX_MANIFEST_BYTES) {
      this->request_body_error_ = true;
      this->request_body_.clear();
      return;
    }
    this->request_body_.append(reinterpret_cast<const char *>(data), len);
    return;
  }

  if (route_is_(request, PREFLIGHT_WRITER_PATH)) {
    if (index == 0) {
      this->request_body_error_ = false;
      this->request_session_error_ = !this->session_token_matches_(request);
      this->request_rate_limited_ = false;
      this->request_retry_after_ms_ = 0;
      this->request_writer_final_ = false;
      this->request_write_permit_error_ = false;
      this->request_write_permit_missing_ = false;
      this->request_writer_offset_ = 0;
      if (this->request_session_error_) {
        this->request_rate_limited_ = !allow_rate_(
            this->bad_session_rate_, BAD_SESSION_RATE_LIMIT,
            BAD_SESSION_RATE_PERIOD_MS, millis(), &this->request_retry_after_ms_);
        this->request_body_error_ = true;
        return;
      }

      const auto offset_header = request->get_header(IMAGE_OFFSET_HEADER);
      const auto final_header = request->get_header(IMAGE_FINAL_HEADER);
      uint32_t offset = 0;
      const bool final_valid = final_header.has_value() &&
                               (final_header.value() == "0" ||
                                final_header.value() == "1");
      this->request_writer_final_ = final_valid && final_header.value() == "1";
      const bool offset_valid = offset_header.has_value() &&
                                parse_decimal_u32(offset_header.value(), &offset);
      this->request_writer_offset_ = offset;
      const bool writer_new = this->writer_state_ == WriterState::IDLE ||
                              this->writer_state_ == WriterState::ABORTED;
      const bool writer_active = this->writer_state_ == WriterState::PREPARED ||
                                 this->writer_state_ == WriterState::WRITING;
      const uint32_t expected = writer_new ? this->candidate_manifest_.image_size
                                           : this->writer_expected_size_;
      const uint64_t request_end = static_cast<uint64_t>(offset) + total;
      const bool shape_valid = total > 0 && total <= MAX_WEB_IMAGE_CHUNK_BYTES &&
                               content_type_is_(request, IMAGE_CONTENT_TYPE) &&
                               this->candidate_state_ == CandidateState::VERIFIED &&
                               (writer_new || writer_active) && offset_valid &&
                               offset == this->writer_bytes_seen_ && expected > 0 &&
                               request_end <= expected && final_valid &&
                               (this->request_writer_final_ ? request_end == expected
                                                           : request_end < expected);
      if (!shape_valid) {
        this->request_body_error_ = true;
        if (writer_active) {
          if (offset_valid && offset != this->writer_bytes_seen_)
            this->fail_writer_(WriterFailure::OFFSET_MISMATCH);
          else if (request_end > expected)
            this->fail_writer_(WriterFailure::SIZE_OVERFLOW);
          else
            this->fail_writer_(WriterFailure::INVALID_CHUNK);
        }
        return;
      }

      if (writer_new) {
        this->expire_write_permit_if_needed_();
        if (!this->write_permit_active_) {
          this->request_write_permit_missing_ = true;
          this->request_body_error_ = true;
          return;
        }
        const auto permit = request->get_header(WRITE_PERMIT_HEADER);
        if (!permit.has_value() || !this->write_permit_matches_(permit.value())) {
          this->request_write_permit_error_ = true;
          this->request_body_error_ = true;
          this->request_rate_limited_ = !allow_rate_(
              this->bad_write_permit_rate_, BAD_WRITE_PERMIT_RATE_LIMIT,
              BAD_WRITE_PERMIT_RATE_PERIOD_MS, millis(),
              &this->request_retry_after_ms_);
          return;
        }
        if (!this->begin_writer_dry_run(permit.value())) {
          this->request_body_error_ = true;
          return;
        }
      }
    }
    if (this->request_body_error_) return;
    if (!this->add_writer_dry_run_chunk(
            this->request_writer_offset_ + static_cast<uint32_t>(index), data,
            len)) {
      this->request_body_error_ = true;
      return;
    }
    this->touch_session_();
    return;
  }

  if (!route_is_(request, PREFLIGHT_IMAGE_PATH)) return;
  if (index == 0) {
    this->request_body_error_ = false;
    this->request_session_error_ = !this->session_token_matches_(request);
    this->request_rate_limited_ = false;
    this->request_retry_after_ms_ = 0;
    if (this->request_session_error_) {
      this->request_rate_limited_ = !allow_rate_(
          this->bad_session_rate_, BAD_SESSION_RATE_LIMIT,
          BAD_SESSION_RATE_PERIOD_MS, millis(), &this->request_retry_after_ms_);
    }
    this->request_image_final_ = false;
    const auto offset_header = request->get_header(IMAGE_OFFSET_HEADER);
    const auto final_header = request->get_header(IMAGE_FINAL_HEADER);
    uint32_t offset = 0;
    const bool final_valid = final_header.has_value() &&
                             (final_header.value() == "0" ||
                              final_header.value() == "1");
    this->request_image_final_ = final_valid && final_header.value() == "1";
    this->request_body_error_ =
        this->request_session_error_ ||
        total == 0 || total > MAX_WEB_IMAGE_CHUNK_BYTES ||
        !content_type_is_(request, IMAGE_CONTENT_TYPE) ||
        (this->candidate_state_ != CandidateState::MANIFEST_ACCEPTED &&
         this->candidate_state_ != CandidateState::RECEIVING_IMAGE) ||
        !offset_header.has_value() ||
        !parse_decimal_u32(offset_header.value(), &offset) ||
        offset != this->image_bytes_seen_ || !final_valid ||
        total > this->candidate_manifest_.image_size - this->image_bytes_seen_ ||
        (this->request_image_final_ &&
         this->image_bytes_seen_ + total != this->candidate_manifest_.image_size) ||
        (!this->request_image_final_ &&
         this->image_bytes_seen_ + total == this->candidate_manifest_.image_size);
    if (this->request_body_error_) {
      if (!this->request_session_error_) this->abort_candidate();
      return;
    }
  }
  if (this->request_body_error_) return;
  if (!this->add_candidate_chunk(data, len)) {
    this->request_body_error_ = true;
    return;
  }
  this->touch_session_();
}

void OtaGuard::handle_manifest_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  uint32_t retry_after_ms = 0;
  if (!allow_rate_(this->manifest_rate_, MANIFEST_RATE_LIMIT,
                   MANIFEST_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
    this->request_body_.clear();
    this->request_body_error_ = false;
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (this->request_body_error_ || this->request_body_.empty() ||
      !content_type_is_(request, MANIFEST_CONTENT_TYPE)) {
    this->request_body_.clear();
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"invalid_manifest_request\",\"writes_flash\":false}");
    return;
  }
  const auto key_id = request->get_header(KEY_ID_HEADER);
  const auto signature_b64 = request->get_header(SIGNATURE_HEADER);
  if (!key_id.has_value() || key_id.value() != TRUST_KEY_ID ||
      !signature_b64.has_value() ||
      signature_b64.value().size() != ED25519_SIGNATURE_B64_LENGTH) {
    this->request_body_.clear();
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"invalid_signature_headers\",\"writes_flash\":false}");
    return;
  }
  std::array<uint8_t, 64> signature{};
  const size_t decoded = base64_decode(signature_b64.value(), signature.data(),
                                       signature.size());
  if (decoded != signature.size()) {
    sodium_memzero(signature.data(), signature.size());
    this->request_body_.clear();
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"invalid_signature_encoding\",\"writes_flash\":false}");
    return;
  }
  const bool accepted = this->begin_candidate(
      reinterpret_cast<const uint8_t *>(this->request_body_.data()),
      this->request_body_.size(), signature.data(), signature.size(),
      ESPHOME_PROJECT_VERSION, false);
  sodium_memzero(signature.data(), signature.size());
  this->request_body_.clear();
  if (accepted && !this->create_session_token_()) {
    this->abort_candidate();
    this->send_json_(request, 500,
                     "{\"ok\":false,\"error\":\"session_token_failure\",\"writes_flash\":false}");
    return;
  }
  const auto snapshot = this->candidate_snapshot();
  if (accepted)
    this->send_json_with_session_(request, 202, candidate_json(snapshot));
  else
    this->send_json_(request, 422, candidate_json(snapshot));
}

void OtaGuard::handle_image_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  if (this->request_rate_limited_) {
    const uint32_t retry_after_ms = this->request_retry_after_ms_;
    this->request_rate_limited_ = false;
    this->request_retry_after_ms_ = 0;
    this->request_session_error_ = false;
    this->request_body_error_ = false;
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (this->request_session_error_) {
    this->request_session_error_ = false;
    this->request_body_error_ = false;
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  if (this->request_body_error_) {
    this->send_json_(request, 400, candidate_json(this->candidate_snapshot()));
    return;
  }
  if (!this->request_image_final_) {
    this->send_json_(request, 202, candidate_json(this->candidate_snapshot()));
    return;
  }
  if (!this->finish_candidate()) {
    this->send_json_(request, 422, candidate_json(this->candidate_snapshot()));
    return;
  }
  this->send_json_(request, 200, candidate_json(this->candidate_snapshot()));
}

void OtaGuard::handle_status_request_(
    web_server_idf::AsyncWebServerRequest *request) const {
  const uint32_t now = millis();
  const auto snapshot = this->candidate_snapshot();
  const auto base = candidate_json(snapshot);
  if (base.size() < 2 || base.back() != '}') {
    this->send_json_(request, 500, base);
    return;
  }
  std::string body = base;
  body.pop_back();
  const auto writer = this->writer_snapshot();
  char suffix[512];
  const int written = snprintf(
      suffix, sizeof(suffix),
      ",\"session_active\":%s,\"session_remaining_ms\":%" PRIu32
      ",\"session_idle_remaining_ms\":%" PRIu32
      ",\"commit_arm_active\":%s,\"commit_arm_remaining_ms\":%" PRIu32
      ",\"write_permit_active\":%s,\"write_permit_remaining_ms\":%" PRIu32
      ",\"writer_state\":\"%s\",\"writer_failure\":\"%s\""
      ",\"writer_bytes_seen\":%" PRIu32 ",\"writer_expected_bytes\":%" PRIu32 "}",
      this->session_token_active_ ? "true" : "false",
      this->session_remaining_ms_(now), this->session_idle_remaining_ms_(now),
      this->commit_arm_active_ ? "true" : "false",
      this->commit_arm_remaining_ms_(now),
      this->write_permit_active_ ? "true" : "false",
      this->write_permit_remaining_ms_(now), writer_state_name(writer.state),
      writer_failure_name(writer.failure), writer.bytes_seen,
      writer.expected_bytes);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(suffix)) {
    this->send_json_(request, 500,
                     "{\"state\":\"failed\",\"error\":\"response_overflow\",\"writes_flash\":false}");
    return;
  }
  body.append(suffix, static_cast<size_t>(written));
  this->send_json_(request, 200, body);
}

void OtaGuard::handle_reset_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  uint32_t retry_after_ms = 0;
  if (!allow_rate_(this->reset_rate_, RESET_RATE_LIMIT,
                   RESET_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (request->contentLength() != 0) {
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"reset_body_must_be_empty\",\"writes_flash\":false}");
    return;
  }
  if (this->session_token_active_ && !this->session_token_matches_(request)) {
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  this->reset_candidate();
  this->request_body_.clear();
  this->request_body_error_ = false;
  this->request_session_error_ = false;
  this->request_image_final_ = false;
  this->send_json_(request, 200, candidate_json(this->candidate_snapshot()));
}

void OtaGuard::handle_commit_arm_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  uint32_t retry_after_ms = 0;
  if (request->contentLength() != 0) {
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"arm_body_must_be_empty\",\"writes_flash\":false}");
    return;
  }
  if (!this->session_token_matches_(request)) {
    if (!allow_rate_(this->bad_session_rate_, BAD_SESSION_RATE_LIMIT,
                     BAD_SESSION_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
      this->send_rate_limited_(request, retry_after_ms);
      return;
    }
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  if (this->candidate_state_ != CandidateState::VERIFIED) {
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"candidate_not_verified\",\"writes_flash\":false}");
    return;
  }
  if (!allow_rate_(this->commit_arm_rate_, COMMIT_ARM_RATE_LIMIT,
                   COMMIT_ARM_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (!this->create_commit_arm_()) {
    this->send_json_(request, 500,
                     "{\"ok\":false,\"error\":\"commit_arm_failure\",\"writes_flash\":false}");
    return;
  }
  char body[160];
  const int written = snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"commit_arm_active\":true,\"commit_arm_remaining_ms\":%" PRIu32
      ",\"writes_flash\":false}",
      this->commit_arm_remaining_ms_(millis()));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
    this->clear_commit_arm_();
    this->send_json_(request, 500,
                     "{\"ok\":false,\"error\":\"response_overflow\",\"writes_flash\":false}");
    return;
  }
  this->send_json_with_commit_arm_(request, 202,
                                   std::string(body, static_cast<size_t>(written)));
}

void OtaGuard::handle_commit_check_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  uint32_t retry_after_ms = 0;
  if (request->contentLength() != 0) {
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"commit_check_body_must_be_empty\",\"writes_flash\":false}");
    return;
  }
  if (!this->session_token_matches_(request)) {
    if (!allow_rate_(this->bad_session_rate_, BAD_SESSION_RATE_LIMIT,
                     BAD_SESSION_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
      this->send_rate_limited_(request, retry_after_ms);
      return;
    }
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  if (this->candidate_state_ != CandidateState::VERIFIED) {
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"candidate_not_verified\",\"writes_flash\":false}");
    return;
  }
  this->expire_commit_arm_if_needed_();
  if (!this->commit_arm_active_) {
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"commit_arm_missing_or_expired\",\"writes_flash\":false}");
    return;
  }
  if (!this->commit_arm_matches_(request)) {
    if (!allow_rate_(this->bad_commit_arm_rate_, BAD_COMMIT_ARM_RATE_LIMIT,
                     BAD_COMMIT_ARM_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
      this->send_rate_limited_(request, retry_after_ms);
      return;
    }
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_commit_arm\",\"writes_flash\":false}");
    return;
  }
  this->clear_commit_arm_();
  if (!this->create_write_permit_()) {
    this->send_json_(request, 500,
                     "{\"ok\":false,\"error\":\"write_permit_failure\",\"writes_flash\":false}");
    return;
  }
  char body[192];
  const int written = snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"commit_authorized\":true,"
      "\"commit_arm_active\":false,\"write_permit_active\":true,"
      "\"write_permit_remaining_ms\":%" PRIu32
      ",\"writes_flash\":false}",
      this->write_permit_remaining_ms_(millis()));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
    this->clear_write_permit_();
    this->send_json_(request, 500,
                     "{\"ok\":false,\"error\":\"response_overflow\",\"writes_flash\":false}");
    return;
  }
  this->send_json_with_write_permit_(
      request, 200, std::string(body, static_cast<size_t>(written)));
}

void OtaGuard::handle_writer_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  if (this->request_rate_limited_) {
    const uint32_t retry_after_ms = this->request_retry_after_ms_;
    this->request_rate_limited_ = false;
    this->request_retry_after_ms_ = 0;
    this->request_session_error_ = false;
    this->request_write_permit_error_ = false;
    this->request_body_error_ = false;
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (this->request_session_error_) {
    this->request_session_error_ = false;
    this->request_body_error_ = false;
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  if (this->request_write_permit_missing_) {
    this->request_write_permit_missing_ = false;
    this->request_body_error_ = false;
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"write_permit_missing_or_expired\",\"writes_flash\":false}");
    return;
  }
  if (this->request_write_permit_error_) {
    this->request_write_permit_error_ = false;
    this->request_body_error_ = false;
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_write_permit\",\"writes_flash\":false}");
    return;
  }
  if (this->request_body_error_) {
    this->request_body_error_ = false;
    this->send_json_(request, 400, writer_json(this->writer_snapshot()));
    return;
  }
  if (!this->request_writer_final_) {
    this->send_json_(request, 202, writer_json(this->writer_snapshot()));
    return;
  }
  if (!this->finish_writer_dry_run()) {
    this->send_json_(request, 422, writer_json(this->writer_snapshot()));
    return;
  }
  this->send_json_(request, 200, writer_json(this->writer_snapshot()));
}

void OtaGuard::handle_writer_abort_request_(
    web_server_idf::AsyncWebServerRequest *request) {
  uint32_t retry_after_ms = 0;
  if (request->contentLength() != 0) {
    this->send_json_(request, 400,
                     "{\"ok\":false,\"error\":\"write_abort_body_must_be_empty\",\"writes_flash\":false}");
    return;
  }
  if (!this->session_token_matches_(request)) {
    if (!allow_rate_(this->bad_session_rate_, BAD_SESSION_RATE_LIMIT,
                     BAD_SESSION_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
      this->send_rate_limited_(request, retry_after_ms);
      return;
    }
    this->send_json_(request, 403,
                     "{\"ok\":false,\"error\":\"invalid_session\",\"writes_flash\":false}");
    return;
  }
  if (this->candidate_state_ != CandidateState::VERIFIED) {
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"candidate_not_verified\",\"writes_flash\":false}");
    return;
  }
  if (!allow_rate_(this->write_abort_rate_, WRITE_ABORT_RATE_LIMIT,
                   WRITE_ABORT_RATE_PERIOD_MS, millis(), &retry_after_ms)) {
    this->send_rate_limited_(request, retry_after_ms);
    return;
  }
  if (this->writer_state_ == WriterState::ABORTED) {
    this->send_json_(request, 200, writer_json(this->writer_snapshot()));
    return;
  }
  if (this->writer_state_ != WriterState::PREPARED &&
      this->writer_state_ != WriterState::WRITING &&
      this->writer_state_ != WriterState::FAILED) {
    this->send_json_(request, 409,
                     "{\"ok\":false,\"error\":\"writer_not_abortable\",\"writes_flash\":false}");
    return;
  }
  this->clear_commit_arm_();
  this->clear_write_permit_();
  this->abort_writer_dry_run();
  this->touch_session_();
  this->send_json_(request, 200, writer_json(this->writer_snapshot()));
}

void OtaGuard::handleRequest(web_server_idf::AsyncWebServerRequest *request) {
  std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);
  this->expire_session_if_needed_();
  this->expire_commit_arm_if_needed_();
  this->expire_write_permit_if_needed_();
  if (request->method() == HTTP_GET && route_is_(request, PREFLIGHT_STATUS_PATH)) {
    this->handle_status_request_(request);
    return;
  }
  if (request->method() != HTTP_POST) {
    this->send_json_(request, 405,
                     "{\"ok\":false,\"error\":\"method_not_allowed\",\"writes_flash\":false}");
    return;
  }
  if (route_is_(request, PREFLIGHT_MANIFEST_PATH)) {
    this->handle_manifest_request_(request);
  } else if (route_is_(request, PREFLIGHT_IMAGE_PATH)) {
    this->handle_image_request_(request);
  } else if (route_is_(request, PREFLIGHT_RESET_PATH)) {
    this->handle_reset_request_(request);
  } else if (route_is_(request, PREFLIGHT_COMMIT_ARM_PATH)) {
    this->handle_commit_arm_request_(request);
  } else if (route_is_(request, PREFLIGHT_COMMIT_CHECK_PATH)) {
    this->handle_commit_check_request_(request);
  } else if (route_is_(request, PREFLIGHT_WRITER_PATH)) {
    this->handle_writer_request_(request);
  } else if (route_is_(request, PREFLIGHT_WRITER_ABORT_PATH)) {
    this->handle_writer_abort_request_(request);
  } else {
    this->send_json_(request, 404,
                     "{\"ok\":false,\"error\":\"not_found\",\"writes_flash\":false}");
  }
}

}  // namespace esphome::usp_ota_guard
