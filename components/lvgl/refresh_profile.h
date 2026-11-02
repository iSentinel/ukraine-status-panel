#pragma once

#include <cstdint>
#include <limits>

namespace esphome {
namespace lvgl {

// Diagnostic arithmetic uses unsigned differences so millis() may wrap once.
// Intervals must be shorter than the uint32_t clock period (about 49 days).
struct RefreshProfileSample {
  uint32_t handler_ms{}, pre_ms{}, render_ms{}, post_ms{};
  uint32_t dispatch_ms{}, layout_ms{}, prepare_ms{};
  bool render_seen{}, ready_seen{};
};

// Keep the early samples, then limit slow-operation logs to one per second.
// Saturation prevents the startup log window from reopening after counter wrap.
class ProfileLogWindow {
 public:
  bool observe(uint32_t now, uint32_t elapsed, uint32_t threshold, uint32_t early) {
    sequence = next_;
    if (next_ != std::numeric_limits<uint32_t>::max())
      ++next_;
    if (sequence < early || (elapsed >= threshold && (!logged_ || now - last_ms_ >= 1000))) {
      last_ms_ = now;
      logged_ = true;
      return true;
    }
    return false;
  }
  uint32_t sequence{};

 private:
  uint32_t next_{};
  uint32_t last_ms_{};
  bool logged_{};
};

// Observe events without requesting layouts, refreshes, or display transfers.
class RefreshProfile {
 public:
  void begin(uint32_t now) {
    *this = RefreshProfile{};
    active_ = true;
    started_ = now;
  }
  void refresh_start(uint32_t now) {
    if (active_ && !refresh_seen_) {
      refresh_ = now;
      refresh_seen_ = true;
    }
  }
  void layout_ready(uint32_t now) {
    if (active_ && refresh_seen_ && !render_seen_) {
      layout_ = now;
      layout_seen_ = true;
    }
  }
  void render_start(uint32_t now) {
    if (!active_) return;
    ready_seen_ = false;
    if (!render_seen_) {
      render_ = now;
      render_seen_ = true;
    }
  }
  void ready(uint32_t now) {
    if (active_ && render_seen_) {
      finished_ = now;
      ready_seen_ = true;
    }
  }
  RefreshProfileSample end(uint32_t now) {
    active_ = false;
    RefreshProfileSample sample;
    sample.handler_ms = now - started_;
    sample.pre_ms = render_seen_ ? render_ - started_ : sample.handler_ms;
    sample.render_ms = render_seen_ ? (ready_seen_ ? finished_ : now) - render_ : 0;
    sample.post_ms = ready_seen_ ? now - finished_ : 0;
    sample.dispatch_ms = refresh_seen_ ? refresh_ - started_ : sample.pre_ms;
    sample.layout_ms = layout_seen_ ? layout_ - refresh_ : 0;
    sample.prepare_ms = sample.pre_ms - sample.dispatch_ms - sample.layout_ms;
    sample.render_seen = render_seen_;
    sample.ready_seen = ready_seen_;
    return sample;
  }

 private:
  uint32_t started_{}, refresh_{}, layout_{}, render_{}, finished_{};
  bool active_{}, refresh_seen_{}, layout_seen_{}, render_seen_{}, ready_seen_{};
};

}  // namespace lvgl
}  // namespace esphome
