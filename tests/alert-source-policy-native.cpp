#include "usp_alert_source_policy.h"
#include "neptun_alert_parser.h"
#include "usp_alert_palette_0_115_114.h"
#include "neptun_threat_parser.h"
#include <cassert>
#include <cstdio>
using namespace usp_alert_source;
using namespace esphome::usp_neptun;

struct Snapshot {
  bool valid{true}, fetch_ok{true}, alerts_valid{true}, alerts_fetch_ok{true};
  uint32_t received_ms{100}, alerts_received_ms{100}, sequence{7};
  uint16_t alert_unknown_regions{}, alert_unknown_levels{};
  std::array<uint8_t, REGION_ALERT_COUNT> alert_levels{};
  std::array<ThreatCounts, REGION_ALERT_COUNT> active_region_threats{};
};

void precedence() {
  Policy p;
  Frame n;
  assert(p.choose(n, 150, 1000) == Source::NONE);
  n.valid = true; n.states.fill(1); n.received_ms = 100;
  assert(p.choose(n, 150, 1000) == Source::NEPTUN);
  n.valid = false;
  assert(p.choose(n, 180, 1000) == Source::NONE);
  int pending[25]{};
  for (auto &state : pending) state = 4;
  assert(p.receive_api(pending, 25, 3, 200));
  n.valid = true;
  assert(p.choose(n, 201, 1000) == Source::API);
  p.clear_api();
  assert(p.choose(n, 202, 1000) == Source::NEPTUN);
}

void regional_truth() {
  Snapshot s;
  const int city = region_alert_index(31), oblast = region_alert_index(14);
  s.active_region_threats[city].ballistic = 1;
  s.active_region_threats[oblast].uav = 2;
  assert(region_state(s, 31, 110, 1000, region_alert_index) == 64);
  assert(region_state(s, 14, 110, 1000, region_alert_index) == 2);
  auto frame = neptun_frame(s, 110, 1000, region_alert_index);
  assert(frame.valid && frame.states[9] == 64);  // Kyiv city joins Kyiv map mask.
  // A failed poll is diagnostic only while the last successful snapshot
  // remains inside stale_after. Do not flicker the source to STALE/NO DATA.
  s.fetch_ok = false;
  s.alert_levels[city] = 2;
  assert(region_state(s, 31, 110, 1000, region_alert_index) == 64);
  s.alerts_fetch_ok = false;
  assert(neptun_frame(s, 110, 1000, region_alert_index).valid);
  s.alerts_fetch_ok = true;
  s.alert_unknown_regions = 1;
  assert(!neptun_frame(s, 110, 1000, region_alert_index).valid);
  s.alert_unknown_regions = 0;
  assert(!neptun_frame(s, 1100, 1000, region_alert_index).valid);
}

void colors_and_types() {
  ThreatCounts c;
  c.mig31k = 1;
  assert(state_from_counts(0, c, true) == 1);
  c.unknown = 1;
  assert(state_from_counts(0, c, true) == 8);
  c.kab = 1;
  assert(state_from_counts(0, c, true) == 128);
  c.missile = 1;
  assert(state_from_counts(0, c, true) == 4);
  c.ballistic = 1;
  assert(state_from_counts(0, c, true) == 64);
  assert(state_from_counts(0, c, false) == 1);
  assert(state_from_counts(1, c, false) == 8);
  assert(state_from_counts(2, c, false) == 16);
  assert(usp_alert_state::normalize(64 | 128 | 4 | 2) == 64);
  assert(usp_alert_state::normalize(128 | 2) == 128);
  assert(usp_alert_palette::color_hex(0xA855F7, 0) == 0xA855F7);
  assert(usp_alert_palette::color_hex(0xA855F7, 1) == 0xCC79A7);
  assert(usp_alert_palette::color_hex(0xA855F7, 2) == 0xE0E0E0);
}

void wrap() {
  Policy p;
  std::array<int, 25> states{}; states.fill(4);
  assert(p.receive_api(states.data(), 25, 0, UINT32_MAX - 20));
  Frame n;
  assert(p.choose(n, 10, 50) == Source::API);
  assert(p.choose(n, 40, 50) == Source::NONE);
  states[0] = 256;
  assert(!p.receive_api(states.data(), 25, 0, 0));
}

void selected_region() {
  Policy p;
  Snapshot s;
  s.active_region_threats[region_alert_index(31)].ballistic = 1;
  const auto n = neptun_frame(s, 110, 1000, region_alert_index);
  p.choose(n, 110, 1000);
  const auto selected = selected_for(p, s, 31, 110, 1000, region_alert_index);
  assert(selected.valid && selected.supported && selected.state == 64);
  p.choose(Frame{}, 111, 1000);
  assert(!selected_for(p, s, 31, 111, 1000, region_alert_index).valid);
}

void unchanged_snapshot() {
  Policy p;
  Snapshot s;
  const auto first = neptun_frame(s, 110, 1000, region_alert_index);
  p.choose(first, 110, 1000);
  s.alerts_received_ms = 900;
  ++s.sequence;
  const auto next = neptun_frame(s, 910, 1000, region_alert_index);
  assert(next.signature == first.signature);
  assert(next.received_ms != first.received_ms);
  assert(p.choose(next, 910, 1000) == Source::NEPTUN);
  assert(p.chosen().received_ms == 900);
}

int main() {
  precedence(); regional_truth(); colors_and_types(); wrap(); selected_region();
  unchanged_snapshot();
  std::puts("SOURCE_POLICY=PASS Neptun_only single_poll_grace stale_ttl "
            "unavailable_no_fallback API_override independent_freshness "
            "ballistic_priority unknown_regions city_merge rollover");
}
