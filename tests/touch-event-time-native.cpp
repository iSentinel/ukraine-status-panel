#include "usp_touch_event_time.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using usp_touch_event::EventTime;
using usp_touch_event::Kind;

int main() {
  EventTime event;
  uint32_t now = 0;
  assert(!event.get(Kind::PRESS, now));
  event.press(100);
  assert(event.get(Kind::PRESS, now) && now == 100);
  assert(event.get(Kind::POINT, now) && now == 100);
  assert(!event.get(Kind::RELEASE, now));
  event.point(450);
  assert(event.get(Kind::PRESS, now) && now == 100);
  assert(event.get(Kind::POINT, now) && now == 450);
  event.release(1000);
  assert(event.get(Kind::PRESS, now) && now == 100);
  assert(event.get(Kind::RELEASE, now) && now == 1000);
  event.point(1100);
  assert(event.get(Kind::POINT, now) && now == 450);
  event.clear();
  assert(!event.get(Kind::PRESS, now));
  assert(!event.get(Kind::POINT, now));
  assert(!event.get(Kind::RELEASE, now));
  event.press(UINT32_MAX - 20U);
  event.point(5U);
  event.release(25U);
  assert(event.get(Kind::PRESS, now) && now == UINT32_MAX - 20U);
  assert(event.get(Kind::POINT, now) && now == 5U);
  assert(event.get(Kind::RELEASE, now) && now == 25U);
  std::puts("TOUCH_EVENT_TIME_NATIVE=PASS press_preserved=1 point_latest=1 release=1 rollover=1");
}
