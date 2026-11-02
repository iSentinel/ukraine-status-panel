"""Execute OTA guard loop/reset/snapshot under controlled parallel contexts."""

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
using namespace std::chrono_literals;
static std::mutex gate;
static std::condition_variable entered;
static bool release_writer=false;
static std::atomic<bool> writer_active=false, overlap=false;
static std::atomic<int> expires=0, freed=0;
struct mbedtls_sha256_context {};
static void mbedtls_sha256_free(mbedtls_sha256_context *) { ++freed; }
namespace esphome::usp_ota_guard {
enum class CandidateState { IDLE, RECEIVING_IMAGE, ABORTED };
enum class CandidateFailure { NONE };
enum class ManifestStatus { INVALID_ARGUMENT };
enum class ImageStatus { IDLE };
struct ManifestValidationResult {
  std::string target_version;
  uint32_t image_size{0};
};
struct CandidateSnapshot {
  CandidateState state;
  CandidateFailure failure;
  ManifestStatus manifest_status;
  ImageStatus image_status;
  std::string target_version;
  uint32_t bytes_seen,expected_bytes;
};
class OtaGuard {
 public:
  mutable std::recursive_mutex state_mutex_;
  CandidateState candidate_state_{CandidateState::RECEIVING_IMAGE};
  CandidateFailure candidate_failure_{CandidateFailure::NONE};
  ManifestStatus candidate_manifest_status_{ManifestStatus::INVALID_ARGUMENT};
  ImageStatus candidate_image_status_{ImageStatus::IDLE};
  ManifestValidationResult candidate_manifest_{"candidate",100};
  std::array<uint8_t,128> image_prefix_{};
  size_t image_prefix_length_{3};
  mbedtls_sha256_context image_sha_{};
  bool image_sha_active_{true};
  ImageStatus image_status_{ImageStatus::IDLE};
  uint32_t image_bytes_seen_{3},expected_image_size_{100};
  std::string expected_image_sha256_{"old"};
  void loop();
  void reset_candidate();
  void reset_image_preflight();
  CandidateSnapshot candidate_snapshot() const;
  void clear_session_token_() {
    std::unique_lock<std::mutex> lock(gate);
    writer_active=true;
    entered.notify_one();
    entered.wait(lock,[]{return release_writer;});
    writer_active=false;
  }
  void expire_session_if_needed_() {
    if(writer_active) overlap=true;
    ++expires;
  }
  void expire_commit_arm_if_needed_() {
    if(writer_active) overlap=true;
    ++expires;
  }
  void expire_write_permit_if_needed_() {
    if(writer_active) overlap=true;
    ++expires;
  }
};
"""
CASES = r"""
}
int main() {
  using namespace esphome::usp_ota_guard;
  OtaGuard guard;
  std::thread http([&]{guard.reset_candidate();});
  {
    std::unique_lock<std::mutex> lock(gate);
    assert(entered.wait_for(lock,2s,[]{return writer_active.load();}));
  }
  const auto start=std::chrono::steady_clock::now();
  guard.loop();
  const auto wait=std::chrono::steady_clock::now()-start;
  auto reader=std::async(std::launch::async,[&]{return guard.candidate_snapshot();});
  bool snapshot_overlapped=reader.wait_for(30ms)==std::future_status::ready;
  {
    std::lock_guard<std::mutex> lock(gate);
    release_writer=true;
  }
  entered.notify_all();
  http.join();
  const auto snapshot=reader.get();
  std::cout<<"LOOP_EXPIRED_DURING_MUTATION="<<overlap
           <<" SNAPSHOT_DURING_MUTATION="<<snapshot_overlapped<<std::endl;
  assert(!overlap && !snapshot_overlapped);
  assert(wait<100ms);
  assert(snapshot.state==CandidateState::IDLE && snapshot.bytes_seen==0);
  assert(freed==1);
  guard.loop();
  assert(expires==3);
  std::cout<<"OTA_GUARD_EXCLUSION=PASS nonblocking_loop nested_reset coherent_snapshot"
              " idle_expiry controlled_thread_boundaries ASAN_UBSAN"<<std::endl;
}
"""


def method(source, name):
    """Extract one exact method, preserving its real synchronization decisions."""
    position = source.index("OtaGuard::" + name + "(")
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    depth, end = 1, opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def native_source():
    """Use actual entrypoints with explicit crypto/session boundary doubles."""
    source = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text()
    names = (
        "loop",
        "reset_candidate",
        "reset_image_preflight",
        "candidate_snapshot",
    )
    return FIXTURE + "".join(method(source, name) for name in names) + CASES


def main():
    """Run server-only thread checks; never call the panel or flash backends."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-ota-exclusion-") as temporary:
        work = Path(temporary)
        source, binary = work / "test.cpp", work / "test"
        source.write_text(native_source())
        subprocess.run(
            [
                "g++",
                "-std=c++20",
                "-g",
                "-pthread",
                "-fsanitize=address,undefined",
                "-fno-pie",
                "-no-pie",
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run([str(binary)], timeout=15, check=False)
        logger.info("Actual OTA guard exclusion exit: {}", result.returncode)
        raise SystemExit(0 if result.returncode == 0 else 1)


if __name__ == "__main__":
    main()
