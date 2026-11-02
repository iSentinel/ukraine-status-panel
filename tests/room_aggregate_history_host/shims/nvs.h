#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using nvs_handle_t = uint32_t;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
constexpr esp_err_t ESP_FAIL = 2;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };

namespace marker_host {
inline bool namespace_exists = false;
inline bool blob_exists = false;
inline std::vector<uint8_t> blob;
inline int read_error = 0;
inline int write_error = 0;
inline int commit_error = 0;
inline bool write_before_error = false;
inline bool read_error_after_write = false;
inline size_t partial_write_bytes = 0;
inline unsigned writes = 0;
inline unsigned writable_opens = 0;

/** Reset only the test storage between independent cases. */
inline void reset() {
  namespace_exists = false;
  blob_exists = false;
  blob.clear();
  read_error = write_error = commit_error = 0;
  write_before_error = false;
  read_error_after_write = false;
  partial_write_bytes = 0;
  writes = writable_opens = 0;
}
}  // namespace marker_host

/** A read-only open cannot create a namespace. */
inline esp_err_t nvs_open(const char *, nvs_open_mode_t mode, nvs_handle_t *out) {
  if (marker_host::read_error) return marker_host::read_error;
  if (!marker_host::namespace_exists && mode == NVS_READONLY)
    return ESP_ERR_NVS_NOT_FOUND;
  if (mode == NVS_READWRITE) {
    ++marker_host::writable_opens;
    marker_host::namespace_exists = true;
  }
  *out = 1;
  return ESP_OK;
}

/** Return exact durable bytes, including malformed sizes used by tests. */
inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *data, size_t *size) {
  if (marker_host::read_error) return marker_host::read_error;
  if (!marker_host::blob_exists) return ESP_ERR_NVS_NOT_FOUND;
  if (data == nullptr) {
    *size = marker_host::blob.size();
    return ESP_OK;
  }
  if (*size < marker_host::blob.size()) return ESP_FAIL;
  std::memcpy(data, marker_host::blob.data(), marker_host::blob.size());
  *size = marker_host::blob.size();
  return ESP_OK;
}

/** Model both a rejected write and a completed write with an error result. */
inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *data, size_t size) {
  ++marker_host::writes;
  const auto *bytes = static_cast<const uint8_t *>(data);
  if (marker_host::partial_write_bytes != 0) {
    const size_t count = marker_host::partial_write_bytes < size
        ? marker_host::partial_write_bytes : size;
    marker_host::blob.assign(bytes, bytes + count);
    marker_host::blob_exists = true;
    return marker_host::write_error ? marker_host::write_error : ESP_FAIL;
  }
  if (!marker_host::write_error || marker_host::write_before_error) {
    marker_host::blob.assign(bytes, bytes + size);
    marker_host::blob_exists = true;
    if (marker_host::read_error_after_write) marker_host::read_error = ESP_FAIL;
  }
  return marker_host::write_error;
}

inline esp_err_t nvs_commit(nvs_handle_t) { return marker_host::commit_error; }
inline void nvs_close(nvs_handle_t) {}
