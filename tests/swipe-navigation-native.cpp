#include "usp_swipe_navigation.h"

#include <cassert>
#include <iostream>

using usp_lcd_gesture::Action;
using usp_lcd_gesture::Gesture;

void check_taps_and_drags() {
  Gesture gesture;
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(307, 205, 250) == Action::NONE);
  assert(!gesture.consumed());
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(221, 200, 250) == Action::NONE);
  assert(gesture.consumed());
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(220, 200, 250) == Action::NEXT);
  assert(gesture.consumed());
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(400, 200, 250) == Action::PREVIOUS);
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(200, 260, 250) == Action::NONE);
  assert(gesture.consumed());
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.release(300, 300, 250) == Action::NONE);
  assert(gesture.consumed());
  gesture.press(300, 200, 100, true, false, false);
  assert(gesture.sample(300, 260, 170) == Action::NONE);
  assert(gesture.release(100, 200, 300) == Action::NONE);
  assert(gesture.consumed());
}

void check_timing_and_blocked_controls() {
  Gesture gesture;
  for (const bool enabled : {false, true}) {
    gesture.press(300, 200, 100, enabled, true, false);
    assert(gesture.release(100, 200, 250) == Action::NONE);
    assert(!gesture.consumed());
  }
  gesture.press(300, 200, 100, false, false, false);
  assert(gesture.release(100, 200, 250) == Action::NONE);
  assert(!gesture.consumed());
  for (uint32_t duration : {39U, 701U, 1500U}) {
    gesture.press(300, 200, 100, true, false, false);
    assert(gesture.release(100, 200, 100 + duration) == Action::NONE);
    assert(gesture.consumed());
  }
  gesture.press(300, 200, UINT32_MAX - 50, true, false, false);
  assert(gesture.release(100, 200, 99) == Action::NEXT);
  // The next contact must clear suppression left by the previous drag.
  gesture.press(300, 200, 200, true, false, false);
  assert(!gesture.consumed());
  assert(gesture.release(300, 200, 350) == Action::NONE);
  gesture.press(300, 200, 100, true, false, false, false);
  assert(gesture.release(100, 200, 250) == Action::NONE);
  assert(gesture.consumed());
  gesture.press(300, 200, 100, true, false, false, false);
  assert(gesture.release(300, 200, 250) == Action::NONE);
  assert(!gesture.consumed());
}

void check_local_escape_and_boundaries() {
  Gesture gesture;
  gesture.press(150, 30, 100, true, false, true);
  assert(gesture.sample(154, 32, 999) == Action::NONE);
  assert(gesture.sample(154, 32, 1000) == Action::MENU);
  assert(gesture.sample(154, 32, 1500) == Action::NONE);
  assert(gesture.release(154, 32, 1600) == Action::NONE);
  assert(gesture.consumed());
  gesture.press(150, 30, 100, true, false, true);
  assert(gesture.sample(180, 30, 500) == Action::NONE);
  assert(gesture.sample(150, 30, 1200) == Action::NONE);
  assert(gesture.release(150, 30, 1250) == Action::NONE);
  for (unsigned saved = 0; saved < 256; ++saved)
    assert(usp_lcd_gesture::fullscreen_mode(saved) == (saved == 1));
  assert(usp_lcd_gesture::adjacent_page(0, Action::PREVIOUS) == 9);
  assert(usp_lcd_gesture::adjacent_page(4, Action::NEXT) == 12);
  assert(usp_lcd_gesture::adjacent_page(1, Action::NEXT) == 2);
  assert(usp_lcd_gesture::adjacent_page(3, Action::PREVIOUS) == 2);
  assert(usp_lcd_gesture::adjacent_page(12, Action::NEXT) == 9);
  assert(usp_lcd_gesture::adjacent_page(12, Action::PREVIOUS) == 4);
  assert(usp_lcd_gesture::adjacent_page(5, Action::NEXT) == 0);
  assert(usp_lcd_gesture::adjacent_page(5, Action::PREVIOUS) == 9);
  assert(usp_lcd_gesture::adjacent_page(5, Action::NONE) == 5);
  assert(usp_lcd_gesture::adjacent_page(10, Action::NEXT) == 0);
  assert(usp_lcd_gesture::adjacent_page(10, Action::PREVIOUS) == 9);
  assert(usp_lcd_gesture::adjacent_page(10, Action::NONE) == 10);
}

void check_physical_stroke_and_cycles() {
  Gesture gesture;
  // Actual .229 GT911 stroke: x15,y290 -> x281,y271 over280ms.
  gesture.press(15, 290, 100, true, false, false);
  assert(gesture.release(281, 271, 380) == Action::PREVIOUS);
  assert(usp_lcd_gesture::adjacent_page(0, Action::PREVIOUS) == 9);
  for (const Action action : {Action::NEXT, Action::PREVIOUS}) {
    uint8_t page = 0;
    for (unsigned index = 1; index <= 14; ++index) {
      page = usp_lcd_gesture::adjacent_page(page, action);
      assert(page != 5 && page != 8 && page != 10);
      assert((page == 0) == (index % 7 == 0));
    }
  }
  for (uint8_t page = 0; page < 9; ++page) {
    assert(usp_lcd_gesture::adjacent_page(page, Action::NONE) == page);
    assert(usp_lcd_gesture::adjacent_page(page, Action::MENU) == page);
  }
}

int main() {
  check_physical_stroke_and_cycles();
  check_taps_and_drags();
  check_timing_and_blocked_controls();
  check_local_escape_and_boundaries();
  std::cout << "SWIPE_NAVIGATION_NATIVE=PASS taps blocked diagonal wrap escape\n";
}
