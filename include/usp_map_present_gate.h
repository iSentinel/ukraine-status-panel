#pragma once

namespace usp_map_present_gate {

/** Per-cache render/present handshake; it owns no LVGL object or shared state. */
class Gate {
 public:
  void arm() {
    pending_ = true;
    drawing_ = false;
  }

  bool consume(bool active_screen, bool render_start, bool refresh_ready) {
    if (!pending_ || !active_screen) return false;
    if (render_start) drawing_ = true;
    if (!refresh_ready || !drawing_) return false;
    pending_ = false;
    return true;
  }

  bool pending() const { return pending_; }
  bool drawing() const { return drawing_; }

 private:
  bool pending_{};
  bool drawing_{};
};

}  // namespace usp_map_present_gate
