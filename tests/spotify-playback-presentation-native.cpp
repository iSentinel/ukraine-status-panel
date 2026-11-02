#include "playback_presentation.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using usp_spotify_playback::ResponseKind;
using usp_spotify_playback_worker::PlaybackState;
using usp_spotify_presentation::Mode;
using usp_spotify_tokens::Phase;

static PlaybackState track_state(uint32_t updated, bool playing = true) {
  PlaybackState state{};
  state.kind = ResponseKind::JSON;
  state.updated_at = updated;
  auto &s = state.snapshot;
  s.valid = true;
  s.has_item = true;
  s.is_playing = playing;
  s.type = usp_spotify_playback::ItemType::TRACK;
  s.progress_ms = 12345;
  s.duration_ms = 201000;
  s.volume_percent = 59;
  s.supports_volume = true;
  s.device_active = true;
  std::strcpy(s.item_name.data(), "Пісня 🎵");
  std::strcpy(s.artists.data(), "Артист A, Artist B");
  std::strcpy(s.context_name.data(), "Альбом");
  std::strcpy(s.device_name.data(), "Kitchen");
  return state;
}

static void mode_cases() {
  PlaybackState state{};
  assert(usp_spotify_presentation::project(Phase::IDLE, state, 10).mode == Mode::DISCONNECTED);
  assert(usp_spotify_presentation::project(Phase::EXCHANGING, state, 10).mode == Mode::CONNECTING);
  assert(usp_spotify_presentation::project(Phase::REAUTH, state, 10).mode == Mode::REAUTH);
  assert(usp_spotify_presentation::project(Phase::CONNECTED, state, 10).mode == Mode::NO_PLAYER);
  state.kind = ResponseKind::FORBIDDEN;
  assert(usp_spotify_presentation::project(Phase::CONNECTED, state, 10).mode == Mode::FORBIDDEN);
  state.kind = ResponseKind::RATE_LIMIT;
  assert(usp_spotify_presentation::project(Phase::CONNECTED, state, 10).mode == Mode::RATE_LIMIT);
  state.kind = ResponseKind::RETRY;
  assert(usp_spotify_presentation::project(Phase::CONNECTED, state, 10).mode == Mode::RETRY);
  state.kind = ResponseKind::REAUTH;
  assert(usp_spotify_presentation::project(Phase::CONNECTED, state, 10).mode == Mode::REAUTH);
}

static void active_case() {
  const auto state = track_state(1000);
  const auto view = usp_spotify_presentation::project(Phase::CONNECTED, state, 3000);
  assert(view.mode == Mode::PLAYING && view.playing);
  assert(std::strcmp(view.title.data(), "Пісня 🎵") == 0);
  assert(std::strcmp(view.subtitle.data(), "Артист A, Artist B") == 0);
  assert(std::strcmp(view.context.data(), "Альбом") == 0);
  assert(std::strcmp(view.device.data(), "Kitchen") == 0);
  assert(view.position_ms == 14345 && view.duration_ms == 201000);
  assert(view.progress_permille == 71);
  assert(std::strcmp(view.elapsed.data(), "0:14") == 0);
  assert(std::strcmp(view.duration.data(), "3:21") == 0);
  assert(view.volume_known && view.volume_percent == 59);
}

static void pause_clamp_wrap_cases() {
  auto paused = track_state(1000, false);
  auto view = usp_spotify_presentation::project(Phase::CONNECTED, paused, 51000);
  assert(view.mode == Mode::PAUSED && !view.playing && view.position_ms == 12345);
  paused.snapshot.duration_ms = 1000;
  paused.snapshot.progress_ms = 999999;
  view = usp_spotify_presentation::project(Phase::CONNECTED, paused, 51000);
  assert(view.position_ms == 1000 && view.progress_permille == 1000);

  auto wrapped = track_state(std::numeric_limits<uint32_t>::max() - 1000U, true);
  wrapped.snapshot.progress_ms = 1000;
  wrapped.snapshot.duration_ms = 10000;
  view = usp_spotify_presentation::project(Phase::CONNECTED, wrapped, 999U);
  assert(view.position_ms == 3000 && view.progress_permille == 300);

  auto unknown = track_state(0, false);
  unknown.snapshot.volume_percent = usp_spotify_playback::VOLUME_UNKNOWN;
  unknown.snapshot.supports_volume = false;
  unknown.snapshot.duration_ms = 0;
  view = usp_spotify_presentation::project(Phase::CONNECTED, unknown, 0);
  assert(!view.volume_known && view.progress_permille == 0);
  assert(std::strcmp(view.duration.data(), "--:--") == 0);
}

/** Inactive account phases cannot expose values retained in an older snapshot. */
static void inactive_snapshot_cases() {
  const auto state = track_state(1000);
  for (const auto phase : {Phase::IDLE, Phase::EXCHANGING, Phase::REAUTH}) {
    const auto view = usp_spotify_presentation::project(phase, state, 3000);
    assert(view.title[0] == 0 && view.subtitle[0] == 0 && view.device[0] == 0);
    assert(!view.playing && !view.volume_known && view.position_ms == 0);
    assert(view.progress_permille == 0);
    assert(std::strcmp(view.elapsed.data(), "--:--") == 0);
  }
  auto previous = state;
  previous.kind = ResponseKind::REAUTH;
  assert(usp_spotify_presentation::project(Phase::IDLE, previous, 3000).mode ==
         Mode::DISCONNECTED);
}

int main() {
  mode_cases(); active_case(); pause_clamp_wrap_cases(); inactive_snapshot_cases();
  static_assert(sizeof(usp_spotify_presentation::View) <= 1024);
  std::cout << "SPOTIFY_PLAYBACK_PRESENTATION_NATIVE=PASS modes active pause clamp wrap bounded no_network\n";
}
