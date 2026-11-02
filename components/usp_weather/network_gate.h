#pragma once

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace esphome::usp_network {

static constexpr char HTTPS_GATE_TAG[] = "usp.net.gate";
static constexpr uint32_t HTTPS_GATE_POLL_MS = 10;

enum class HttpsPriority : uint8_t {
  BACKGROUND = 0,
  NORMAL = 1,
  CRITICAL = 2,
};

// Cloud HTTPS clients share scarce ESP32 Wi-Fi/TLS resources. Admission stays
// serialized, but alert traffic has a bounded wait and priority over background
// clients once the current holder releases the gate.
inline std::mutex &https_mutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::atomic<const char *> &https_active_owner() {
  static std::atomic<const char *> owner{nullptr};
  return owner;
}

inline std::atomic<uint32_t> &https_active_since_ms() {
  static std::atomic<uint32_t> started{0};
  return started;
}

inline std::atomic<uint32_t> &https_waiters() {
  static std::atomic<uint32_t> waiters{0};
  return waiters;
}

inline std::atomic<uint32_t> &https_critical_waiters() {
  static std::atomic<uint32_t> waiters{0};
  return waiters;
}

class HttpsTransaction final {
 public:
  explicit HttpsTransaction(
      const char *owner, HttpsPriority priority = HttpsPriority::NORMAL,
      uint32_t wait_timeout_ms = 8000)
      : owner_(owner), priority_(priority), wait_timeout_ms_(wait_timeout_ms),
        lock_(https_mutex(), std::defer_lock) {
    this->acquire_();
  }

  ~HttpsTransaction() {
    if (!this->acquired_) return;
    const uint32_t duration = millis() - this->acquired_ms_;
    https_active_owner().store(nullptr, std::memory_order_release);
    https_active_since_ms().store(0, std::memory_order_release);
    this->lock_.unlock();
    ESP_LOGI(
        HTTPS_GATE_TAG,
        "HTTPS_GATE_RELEASE owner=%s duration_ms=%lu waiters=%lu",
        this->owner_, static_cast<unsigned long>(duration),
        static_cast<unsigned long>(
            https_waiters().load(std::memory_order_relaxed)));
  }

  HttpsTransaction(const HttpsTransaction &) = delete;
  HttpsTransaction &operator=(const HttpsTransaction &) = delete;

  bool acquired() const { return this->acquired_; }

 private:
  bool can_try_() const {
    if (this->priority_ == HttpsPriority::CRITICAL) return true;
    return https_critical_waiters().load(std::memory_order_acquire) == 0;
  }

  void acquire_() {
    const uint32_t queued = millis();
    const bool critical = this->priority_ == HttpsPriority::CRITICAL;
    if (critical) {
      https_critical_waiters().fetch_add(1, std::memory_order_acq_rel);
    }
    bool waiting_logged = false;
    while (!this->acquired_) {
      if (this->can_try_() && this->lock_.try_lock()) {
        this->acquired_ = true;
        break;
      }
      if (!waiting_logged) {
        https_waiters().fetch_add(1, std::memory_order_relaxed);
        waiting_logged = true;
        const char *active =
            https_active_owner().load(std::memory_order_acquire);
        ESP_LOGI(
            HTTPS_GATE_TAG,
            "HTTPS_GATE_WAIT owner=%s active=%s waiters=%lu priority=%u",
            this->owner_, active == nullptr ? "none" : active,
            static_cast<unsigned long>(
                https_waiters().load(std::memory_order_relaxed)),
            static_cast<unsigned>(this->priority_));
      }
      if (millis() - queued >= this->wait_timeout_ms_) break;
      delay(HTTPS_GATE_POLL_MS);
    }
    if (waiting_logged) {
      https_waiters().fetch_sub(1, std::memory_order_relaxed);
    }
    if (critical) {
      https_critical_waiters().fetch_sub(1, std::memory_order_acq_rel);
    }
    if (!this->acquired_) {
      ESP_LOGW(
          HTTPS_GATE_TAG,
          "HTTPS_GATE_TIMEOUT owner=%s wait_ms=%lu priority=%u",
          this->owner_, static_cast<unsigned long>(millis() - queued),
          static_cast<unsigned>(this->priority_));
      return;
    }
    this->acquired_ms_ = millis();
    https_active_since_ms().store(this->acquired_ms_, std::memory_order_release);
    https_active_owner().store(this->owner_, std::memory_order_release);
    ESP_LOGI(
        HTTPS_GATE_TAG,
        "HTTPS_GATE_ACQUIRE owner=%s wait_ms=%lu waiters=%lu priority=%u",
        this->owner_, static_cast<unsigned long>(this->acquired_ms_ - queued),
        static_cast<unsigned long>(
            https_waiters().load(std::memory_order_relaxed)),
        static_cast<unsigned>(this->priority_));
  }

  const char *owner_;
  HttpsPriority priority_;
  uint32_t wait_timeout_ms_{0};
  uint32_t acquired_ms_{0};
  bool acquired_{false};
  std::unique_lock<std::mutex> lock_;
};

}  // namespace esphome::usp_network
