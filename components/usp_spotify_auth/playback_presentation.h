#pragma once

#include "playback_worker_engine.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace usp_spotify_presentation {

enum class Mode : uint8_t {
  DISCONNECTED,
  CONNECTING,
  REAUTH,
  NO_PLAYER,
  PLAYING,
  PAUSED,
  FORBIDDEN,
  RATE_LIMIT,
  RETRY,
};

/** Fixed-size view copied to the main/LVGL task. It owns no provider pointers. */
struct View {
  Mode mode{Mode::DISCONNECTED};
  std::array<char, usp_spotify_playback::MAX_ITEM_NAME + 1> title{};
  std::array<char, usp_spotify_playback::MAX_ARTISTS + 1> subtitle{};
  std::array<char, usp_spotify_playback::MAX_CONTEXT_NAME + 1> context{};
  std::array<char, usp_spotify_playback::MAX_DEVICE_NAME + 1> device{};
  std::array<char, 12> elapsed{};
  std::array<char, 12> duration{};
  uint32_t position_ms{0};
  uint32_t duration_ms{0};
  uint16_t progress_permille{0};
  uint8_t volume_percent{usp_spotify_playback::VOLUME_UNKNOWN};
  usp_spotify_playback::ItemType item_type{usp_spotify_playback::ItemType::NONE};
  bool volume_known{false};
  bool playing{false};
};

namespace detail {

template<size_t N, size_t M>
inline void copy_array(std::array<char, N> &target, const std::array<char, M> &source) {
  static_assert(N > 0 && M > 0);
  target.fill(0);
  const auto end = std::find(source.begin(), source.end(), char(0));
  const size_t count = std::min<size_t>(static_cast<size_t>(end - source.begin()), N - 1);
  if (count) std::memcpy(target.data(), source.data(), count);
  target[count] = 0;
}

inline void format_time(std::array<char, 12> &target, uint32_t milliseconds, bool known) {
  target.fill(0);
  if (!known) {
    std::snprintf(target.data(), target.size(), "--:--");
    return;
  }
  const uint32_t seconds = milliseconds / 1000U;
  const uint32_t hours = seconds / 3600U;
  const uint32_t minutes = (seconds / 60U) % 60U;
  const uint32_t remainder = seconds % 60U;
  if (hours)
    std::snprintf(target.data(), target.size(), "%lu:%02lu:%02lu",
                  static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(remainder));
  else
    std::snprintf(target.data(), target.size(), "%lu:%02lu",
                  static_cast<unsigned long>(seconds / 60U),
                  static_cast<unsigned long>(remainder));
}

inline Mode mode(usp_spotify_tokens::Phase phase,
                 const usp_spotify_playback_worker::PlaybackState &state) {
  using usp_spotify_playback::ResponseKind;
  if (phase == usp_spotify_tokens::Phase::REAUTH) return Mode::REAUTH;
  if (phase == usp_spotify_tokens::Phase::EXCHANGING) return Mode::CONNECTING;
  if (phase != usp_spotify_tokens::Phase::CONNECTED) return Mode::DISCONNECTED;
  if (state.kind == ResponseKind::REAUTH) return Mode::REAUTH;
  if (state.kind == ResponseKind::EMPTY) return Mode::NO_PLAYER;
  if (state.kind == ResponseKind::FORBIDDEN) return Mode::FORBIDDEN;
  if (state.kind == ResponseKind::RATE_LIMIT) return Mode::RATE_LIMIT;
  if (state.kind == ResponseKind::RETRY) return Mode::RETRY;
  if (state.kind != ResponseKind::JSON || !state.snapshot.valid || !state.snapshot.has_item)
    return Mode::NO_PLAYER;
  return state.snapshot.is_playing ? Mode::PLAYING : Mode::PAUSED;
}

/** Copy only metadata owned by the current validated snapshot. */
inline void copy_metadata(View &view,
                          const usp_spotify_playback::PlaybackSnapshot &snapshot) {
  copy_array(view.title, snapshot.item_name);
  copy_array(view.subtitle, snapshot.artists);
  copy_array(view.context, snapshot.context_name);
  copy_array(view.device, snapshot.device_name);
  view.item_type = snapshot.type;
  view.duration_ms = snapshot.duration_ms;
  view.playing = snapshot.is_playing && snapshot.has_item;
  view.volume_percent = snapshot.volume_percent;
  view.volume_known = snapshot.supports_volume &&
      snapshot.volume_percent != usp_spotify_playback::VOLUME_UNKNOWN &&
      snapshot.volume_percent <= 100;
}

/** Interpolate progress locally with the same wrap and duration limits. */
inline void set_position(View &view,
                         const usp_spotify_playback::PlaybackSnapshot &snapshot,
                         uint32_t updated_at, uint32_t now) {
  uint64_t position = snapshot.progress_ms;
  if (view.playing) position += static_cast<uint32_t>(now - updated_at);
  if (view.duration_ms) position = std::min<uint64_t>(position, view.duration_ms);
  view.position_ms = static_cast<uint32_t>(std::min<uint64_t>(position, UINT32_MAX));
  if (view.duration_ms) {
    const uint64_t scaled = static_cast<uint64_t>(view.position_ms) * 1000ULL;
    view.progress_permille = static_cast<uint16_t>(std::min<uint64_t>(1000ULL, scaled / view.duration_ms));
  }
}

}  // namespace detail

/** Build a local presentation; progress interpolation performs no provider I/O. */
inline View project(usp_spotify_tokens::Phase phase,
                    const usp_spotify_playback_worker::PlaybackState &state,
                    uint32_t now) {
  View view{};
  view.mode = detail::mode(phase, state);
  const bool active = view.mode == Mode::PLAYING || view.mode == Mode::PAUSED;
  const bool has_snapshot = active &&
      state.kind == usp_spotify_playback::ResponseKind::JSON && state.snapshot.valid;
  if (!has_snapshot) {
    detail::format_time(view.elapsed, 0, false);
    detail::format_time(view.duration, 0, false);
    return view;
  }

  const auto &snapshot = state.snapshot;
  detail::copy_metadata(view, snapshot);
  detail::set_position(view, snapshot, state.updated_at, now);
  detail::format_time(view.elapsed, view.position_ms, snapshot.has_item);
  detail::format_time(view.duration, view.duration_ms, snapshot.has_item && view.duration_ms > 0);
  return view;
}

static_assert(sizeof(View) <= 1024, "Spotify presentation must stay bounded");

}  // namespace usp_spotify_presentation
