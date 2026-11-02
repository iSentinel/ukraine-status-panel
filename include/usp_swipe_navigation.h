#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace usp_lcd_gesture {

enum class Action : uint8_t { NONE, NEXT, PREVIOUS, MENU };

// Unknown preference values always keep the visible navigation buttons.
constexpr bool fullscreen_mode(uint8_t saved) { return saved == 1; }

// Page IDs remain stable. Settings5 stays outside the normal content circle,
// but a fullscreen swipe can always leave it: next -> Home, previous -> Clock.
// Room10 also exits to Home/Clock without joining the main circle.
// Diagnostics8 remains outside gesture navigation.
constexpr uint8_t MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9};
constexpr unsigned MAIN_PAGE_COUNT = sizeof(MAIN_PAGES) / sizeof(MAIN_PAGES[0]);

constexpr uint8_t adjacent_page(uint8_t page, Action action) {
  if (page == 5) {
    if (action == Action::NEXT) return 0;
    if (action == Action::PREVIOUS) return 9;
    return page;
  }
  if (page == 10) {
    if (action == Action::NEXT) return 0;
    if (action == Action::PREVIOUS) return 9;
    return page;
  }
  for (unsigned index = 0; index < MAIN_PAGE_COUNT; ++index) {
    if (MAIN_PAGES[index] != page) continue;
    if (action == Action::NEXT)
      return MAIN_PAGES[(index + 1) % MAIN_PAGE_COUNT];
    if (action == Action::PREVIOUS)
      return MAIN_PAGES[(index + MAIN_PAGE_COUNT - 1) % MAIN_PAGE_COUNT];
    return page;
  }
  return page;
}

/** Recognize a bounded drag without depending on LVGL or network state. */
class Gesture {
 public:
  void press(int16_t x, int16_t y, uint32_t now, bool enabled,
             bool blocked, bool header, bool allow_swipe = true) {
    this->x_ = x;
    this->y_ = y;
    this->started_ = now;
    this->travel_ = 0;
    this->active_ = enabled && !blocked;
    this->header_ = header;
    this->consumed_ = false;
    this->menu_sent_ = false;
    this->vertical_ = false;
    this->allow_swipe_ = allow_swipe;
  }

  Action sample(int16_t x, int16_t y, uint32_t now) {
    if (!this->active_ || this->menu_sent_) return Action::NONE;
    this->travel_ = std::max(this->travel_,
        std::max(std::abs(int(x) - this->x_), std::abs(int(y) - this->y_)));
    const int dx = std::abs(int(x) - this->x_);
    const int dy = std::abs(int(y) - this->y_);
    if (dy >= 40 && dy > dx) this->vertical_ = true;
    if (this->travel_ >= 24) this->consumed_ = true;
    if (this->header_ && this->travel_ <= 12 && now - this->started_ >= 900) {
      this->consumed_ = true;
      this->menu_sent_ = true;
      return Action::MENU;
    }
    return Action::NONE;
  }

  Action release(int16_t x, int16_t y, uint32_t now) {
    const Action held = this->sample(x, y, now);
    const bool active = this->active_;
    this->active_ = false;
    if (held == Action::MENU) return held;
    if (!active || this->menu_sent_ || !this->allow_swipe_) return Action::NONE;
    const int dx = int(x) - this->x_;
    const int dy = int(y) - this->y_;
    const uint32_t elapsed = now - this->started_;
    if (this->vertical_ || elapsed < 40 || elapsed > 700 || std::abs(dx) < 80 ||
        std::abs(dx) < 2 * std::abs(dy)) return Action::NONE;
    this->consumed_ = true;
    return dx < 0 ? Action::NEXT : Action::PREVIOUS;
  }

  bool consumed() const { return this->consumed_; }

 private:
  int16_t x_{};
  int16_t y_{};
  uint32_t started_{};
  int travel_{};
  bool active_{};
  bool header_{};
  bool consumed_{};
  bool menu_sent_{};
  bool vertical_{};
  bool allow_swipe_{};
};

}  // namespace usp_lcd_gesture
