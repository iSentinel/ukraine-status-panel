#include "ota_write_pipeline.h"

namespace esphome::usp_ota_guard {

const char *OtaWritePipeline::state_name(WritePipelineState state) {
  switch (state) {
    case WritePipelineState::IDLE: return "idle";
    case WritePipelineState::OPEN: return "open";
    case WritePipelineState::WRITING: return "writing";
    case WritePipelineState::FINALIZED: return "finalized";
    case WritePipelineState::ABORTED: return "aborted";
    case WritePipelineState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaWritePipeline::failure_name(WritePipelineFailure failure) {
  switch (failure) {
    case WritePipelineFailure::NONE: return "none";
    case WritePipelineFailure::BACKEND_UNAVAILABLE: return "backend_unavailable";
    case WritePipelineFailure::INVALID_ARGUMENT: return "invalid_argument";
    case WritePipelineFailure::INVALID_STATE: return "invalid_state";
    case WritePipelineFailure::OFFSET_MISMATCH: return "offset_mismatch";
    case WritePipelineFailure::SIZE_OVERFLOW: return "size_overflow";
    case WritePipelineFailure::SIZE_MISMATCH: return "size_mismatch";
    case WritePipelineFailure::BACKEND_BEGIN_FAILED: return "backend_begin_failed";
    case WritePipelineFailure::BACKEND_WRITE_FAILED: return "backend_write_failed";
    case WritePipelineFailure::BACKEND_FINISH_FAILED: return "backend_finish_failed";
  }
  return "unknown";
}

void OtaWritePipeline::clear_counters_() {
  this->expected_bytes_ = 0;
  this->written_bytes_ = 0;
}

void OtaWritePipeline::fail_(WritePipelineFailure failure,
                             bool abort_backend) {
  if (abort_backend && this->backend_ != nullptr) this->backend_->abort();
  this->failure_ = failure;
  this->state_ = WritePipelineState::FAILED;
}

bool OtaWritePipeline::begin(size_t expected_bytes) {
  if (this->state_ != WritePipelineState::IDLE &&
      this->state_ != WritePipelineState::ABORTED) {
    this->fail_(WritePipelineFailure::INVALID_STATE, false);
    return false;
  }
  if (this->backend_ == nullptr) {
    this->fail_(WritePipelineFailure::BACKEND_UNAVAILABLE, false);
    return false;
  }
  if (expected_bytes == 0) {
    this->fail_(WritePipelineFailure::INVALID_ARGUMENT, false);
    return false;
  }

  this->clear_counters_();
  this->failure_ = WritePipelineFailure::NONE;
  if (!this->backend_->begin(expected_bytes)) {
    this->fail_(WritePipelineFailure::BACKEND_BEGIN_FAILED, true);
    return false;
  }
  this->expected_bytes_ = expected_bytes;
  this->state_ = WritePipelineState::OPEN;
  return true;
}

bool OtaWritePipeline::write(size_t offset, const uint8_t *data,
                             size_t length) {
  if (this->state_ != WritePipelineState::OPEN &&
      this->state_ != WritePipelineState::WRITING) {
    this->fail_(WritePipelineFailure::INVALID_STATE, true);
    return false;
  }
  if (data == nullptr || length == 0) {
    this->fail_(WritePipelineFailure::INVALID_ARGUMENT, true);
    return false;
  }
  if (offset != this->written_bytes_) {
    this->fail_(WritePipelineFailure::OFFSET_MISMATCH, true);
    return false;
  }
  if (length > this->expected_bytes_ - this->written_bytes_) {
    this->fail_(WritePipelineFailure::SIZE_OVERFLOW, true);
    return false;
  }
  if (!this->backend_->write(data, length)) {
    this->fail_(WritePipelineFailure::BACKEND_WRITE_FAILED, true);
    return false;
  }

  this->written_bytes_ += length;
  this->state_ = WritePipelineState::WRITING;
  return true;
}

bool OtaWritePipeline::finish() {
  if (this->state_ != WritePipelineState::OPEN &&
      this->state_ != WritePipelineState::WRITING) {
    this->fail_(WritePipelineFailure::INVALID_STATE, true);
    return false;
  }
  if (this->written_bytes_ != this->expected_bytes_) {
    this->fail_(WritePipelineFailure::SIZE_MISMATCH, true);
    return false;
  }
  if (!this->backend_->finish()) {
    this->fail_(WritePipelineFailure::BACKEND_FINISH_FAILED, true);
    return false;
  }

  this->failure_ = WritePipelineFailure::NONE;
  this->state_ = WritePipelineState::FINALIZED;
  return true;
}

void OtaWritePipeline::abort() {
  if (this->backend_ != nullptr &&
      (this->state_ == WritePipelineState::OPEN ||
       this->state_ == WritePipelineState::WRITING ||
       this->state_ == WritePipelineState::FAILED)) {
    this->backend_->abort();
  }
  this->clear_counters_();
  this->failure_ = WritePipelineFailure::NONE;
  this->state_ = WritePipelineState::ABORTED;
}

WritePipelineSnapshot OtaWritePipeline::snapshot() const {
  WritePipelineSnapshot snapshot;
  snapshot.state = this->state_;
  snapshot.failure = this->failure_;
  snapshot.expected_bytes = this->expected_bytes_;
  snapshot.written_bytes = this->written_bytes_;
  return snapshot;
}

}  // namespace esphome::usp_ota_guard
