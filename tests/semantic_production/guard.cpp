#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <iostream>
static unsigned hash_calls;
struct Hash {};
static int mbedtls_sha256_update(Hash *, const uint8_t *, size_t) {
  ++hash_calls;
  return 0;
}
namespace esphome::usp_ota_guard {
enum class WriterState { PREPARED, WRITING, FAILED };
enum class WriterFailure {
  INVALID_STATE, INVALID_CHUNK, OFFSET_MISMATCH, SIZE_OVERFLOW, HASH_FAILURE
};
class OtaGuard {
 public:
  std::recursive_mutex state_mutex_;
  WriterState writer_state_{WriterState::PREPARED};
  WriterFailure failure{WriterFailure::INVALID_STATE};
  bool writer_sha_active_{true};
  uint32_t writer_bytes_seen_{0}, writer_expected_size_{8};
  Hash writer_sha_;
  void fail_writer_(WriterFailure value) {
    failure = value;
    writer_state_ = WriterState::FAILED;
  }
  bool add_writer_dry_run_chunk(uint32_t, const uint8_t *, size_t);
};
// PRODUCTION_METHOD
}
int main() {
  using namespace esphome::usp_ota_guard;
  const uint8_t data[8]{};
  for (uint32_t offset : {1U, 3U, UINT32_MAX}) {
    OtaGuard guard;
    hash_calls = 0;
    assert(!guard.add_writer_dry_run_chunk(offset, data, 4));
    assert(guard.failure == WriterFailure::OFFSET_MISMATCH && hash_calls == 0);
  }
  for (uint32_t split = 1; split < 8; ++split) {
    OtaGuard guard;
    assert(guard.add_writer_dry_run_chunk(0, data, split));
    assert(guard.writer_bytes_seen_ == split);
    assert(guard.add_writer_dry_run_chunk(split, data + split, 8 - split));
    assert(guard.writer_bytes_seen_ == 8);
  }
  std::cout << "OTA_GUARD_CHUNK_PRODUCTION=PASS\n";
}
