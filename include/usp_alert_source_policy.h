#pragma once
#include "usp_alert_region_ids.h"
#include "usp_alert_state_0_115_103.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace usp_alert_source {
enum class Source : uint8_t { NONE = 0, NEPTUN = 2, API = 3 };

struct Frame {
  std::array<uint8_t, 25> states{};
  uint32_t received_ms{}, sequence{}, signature{};
  bool valid{};
};

struct Selected {
  uint16_t region{}, flags{};
  uint32_t sequence{};
  uint8_t state{};
  bool valid{}, supported{};
};

inline uint32_t signature(const std::array<uint8_t, 25> &states) {
  uint32_t value = 2166136261U;
  for (uint8_t state : states) value = (value ^ state) * 16777619U;
  return value;
}

inline bool recent(bool valid, uint32_t received, uint32_t now, uint32_t timeout) {
  return valid && static_cast<uint32_t>(now - received) < timeout;
}

template<class Snapshot>
bool alerts_ready(const Snapshot &s, uint32_t now, uint32_t timeout) {
  // fetch_ok describes only the most recent transport attempt. Keep the
  // last successful alert snapshot usable until its freshness TTL expires.
  return recent(s.alerts_valid, s.alerts_received_ms, now, timeout)
      && s.alert_unknown_regions == 0 && s.alert_unknown_levels == 0;
}

template<class Counts>
uint8_t state_from_counts(uint8_t level, const Counts &c, bool details_fresh) {
  if (details_fresh) {
    if (c.ballistic) return 64;
    if (c.missile) return 4;
    if (c.kab) return 128;
    if (c.uav || c.fpv || c.recon) return 2;
    if (c.unknown) return 8;
  }
  // Carrier observations do not invent an official air alert. Preserve the
  // regional NEPTUN level when no more-specific fresh threat type is known.
  if (level == 2) return 16;  // red: official air alert
  if (level == 1) return 8;   // yellow: regional threat/warning
  return 1;
}

template<class Snapshot, class RegionIndex>
uint8_t region_state(const Snapshot &s, uint16_t region, uint32_t now,
                     uint32_t timeout, RegionIndex index_for) {
  const int index = index_for(region);
  if (index < 0) return 0;
  // A failed retry must not erase still-fresh threat details.
  const bool fresh = recent(s.valid, s.received_ms, now, timeout);
  return state_from_counts(s.alert_levels[static_cast<size_t>(index)],
      s.active_region_threats[static_cast<size_t>(index)], fresh);
}

template<class Snapshot, class RegionIndex>
Frame neptun_frame(const Snapshot &s, uint32_t now, uint32_t timeout, RegionIndex index_for) {
  Frame result;
  result.valid = alerts_ready(s, now, timeout);
  result.received_ms = s.alerts_received_ms;
  result.sequence = s.sequence;
  for (size_t index = 0; index < result.states.size(); ++index) {
    const uint16_t region = usp_alert_rows::region_id_for_index(index);
    uint8_t state = region_state(s, region, now, timeout, index_for);
    if (region == 14) {
      const uint8_t city = region_state(s, 31, now, timeout, index_for);
      state = usp_alert_state::normalize(state | city);
    }
    result.states[index] = state;
  }
  result.signature = signature(result.states);
  return result;
}

class Policy {
 public:
  template<class Value>
  bool receive_api(const Value *states, size_t count, uint32_t sequence, uint32_t now) {
    return receive_(api_, states, count, sequence, now);
  }
  void remember_api_revision(const std::string &revision) { api_revision_ = revision; }
  const std::string &api_revision() const { return api_revision_; }
  void clear_api() { api_.valid = false; }
  const Frame &chosen() const { return chosen_; }
  Source source() const { return source_; }

  Source choose(const Frame &neptun, uint32_t now, uint32_t stale_ms) {
    source_ = Source::NONE;
    if (recent(api_.valid, api_.received_ms, now, stale_ms)) {
      chosen_ = api_; source_ = Source::API;
    } else if (neptun.valid) {
      chosen_ = neptun; source_ = Source::NEPTUN;
    }
    return source_;
  }

 private:
  template<class Value>
  static bool receive_(Frame &frame, const Value *states, size_t count,
                       uint32_t sequence, uint32_t now) {
    if (states == nullptr || count != 25) return false;
    for (size_t index = 0; index < count; ++index) {
      const int value = static_cast<int>(states[index]);
      if (value < 0 || value > 255) return false;
    }
    for (size_t index = 0; index < count; ++index)
      frame.states[index] = static_cast<uint8_t>(states[index]);
    frame.received_ms = now;
    frame.sequence = sequence;
    frame.signature = signature(frame.states);
    frame.valid = true;
    return true;
  }
  Frame api_{}, chosen_{};
  std::string api_revision_;
  Source source_{Source::NONE};
};

template<class Snapshot, class RegionIndex>
Selected selected_for(const Policy &p, const Snapshot &s, uint16_t region,
                      uint32_t now, uint32_t timeout, RegionIndex index_for) {
  Selected value;
  value.region = region;
  if (p.source() == Source::NEPTUN) {
    value.valid = alerts_ready(s, now, timeout);
    value.supported = index_for(region) >= 0;
    value.state = value.supported ? region_state(s, region, now, timeout, index_for) : 0;
    value.sequence = s.sequence;
  } else if (p.source() == Source::API) {
    const uint16_t mask_region = region == 31 ? 14 : region;
    for (size_t i = 0; i < 25; ++i) {
      if (usp_alert_rows::region_id_for_index(i) != mask_region) continue;
      value.state = usp_alert_state::normalize(p.chosen().states[i]);
      value.valid = true;
      value.supported = value.state != 0;
      value.sequence = p.chosen().sequence;
      break;
    }
  }
  return value;
}

inline Policy &policy() {
  static Policy instance;
  return instance;
}
}  // namespace usp_alert_source
