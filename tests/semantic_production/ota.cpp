#include "ota_write_pipeline.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace esphome::usp_ota_guard;
static unsigned writes, aborts, finishes;
static bool begin_ok = true, write_ok = true, finish_ok = true;
namespace esphome::usp_ota_guard {
// Replace only the physical flash boundary; pipeline decisions are production.
OtaFlashBackend::~OtaFlashBackend() = default;
bool OtaFlashBackend::begin(size_t) { return begin_ok; }
bool OtaFlashBackend::write(const uint8_t *, size_t) {
  ++writes;
  return write_ok;
}
bool OtaFlashBackend::finish() { ++finishes; return finish_ok; }
void OtaFlashBackend::abort() { ++aborts; }
}
static const uint8_t DATA[8]{};
static void offsets() {
  for (size_t bad : {size_t(1), size_t(3), std::numeric_limits<size_t>::max()}) {
    OtaFlashBackend backend;
    OtaWritePipeline pipe(&backend);
    writes = aborts = 0;
    assert(pipe.begin(8));
    assert(!pipe.write(bad, DATA, 4));
    assert(pipe.snapshot().failure == WritePipelineFailure::OFFSET_MISMATCH);
    assert(writes == 0 && aborts == 1);
  }
}
static void chunks() {
  for (size_t split = 1; split < 8; ++split) {
    OtaFlashBackend backend;
    OtaWritePipeline pipe(&backend);
    writes = aborts = finishes = 0;
    assert(pipe.begin(8) && pipe.write(0, DATA, split));
    assert(pipe.snapshot().written_bytes == split);
    assert(pipe.write(split, DATA + split, 8 - split));
    assert(pipe.finish());
    assert(pipe.snapshot().written_bytes == 8);
    assert(writes == 2 && aborts == 0 && finishes == 1);
  }
}
static void invalid_and_failures() {
  OtaFlashBackend backend;
  OtaWritePipeline pipe(&backend);
  OtaWritePipeline missing(nullptr);
  assert(!missing.begin(8));
  assert(!pipe.begin(0));
  pipe.abort();
  assert(pipe.begin(8) && !pipe.write(0, nullptr, 1));
  pipe.abort();
  assert(pipe.begin(8) && !pipe.write(0, DATA, 0));
  pipe.abort();
  assert(pipe.begin(8) && !pipe.write(0, DATA, 9));
  pipe.abort();
  assert(pipe.begin(8) && pipe.write(0, DATA, 4) && !pipe.finish());
  assert(pipe.snapshot().failure == WritePipelineFailure::SIZE_MISMATCH);
  pipe.abort();
  write_ok = false;
  assert(pipe.begin(8) && !pipe.write(0, DATA, 4));
  assert(pipe.snapshot().failure == WritePipelineFailure::BACKEND_WRITE_FAILED);
  pipe.abort();
  write_ok = true;
  finish_ok = false;
  assert(pipe.begin(8) && pipe.write(0, DATA, 8) && !pipe.finish());
  pipe.abort();
  finish_ok = true;
  begin_ok = false;
  assert(!pipe.begin(8));
  begin_ok = true;
}
int main() {
  offsets();
  chunks();
  invalid_and_failures();
  std::cout << "OTA_PIPELINE_PRODUCTION=PASS\n";
}
