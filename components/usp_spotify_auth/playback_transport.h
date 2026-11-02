#pragma once

#include "playback_worker_engine.h"
#include <cstddef>
#include <string_view>

namespace esphome::usp_spotify_auth {

constexpr size_t MAX_PLAYBACK_BODY = 32768;

/** Fixed read-only Spotify Player endpoint. No token persistence or control calls. */
class IdfPlaybackTransport final : public usp_spotify_playback_worker::WorkerTransport {
 public:
  static bool available();
  usp_spotify_tokens::HttpResult perform(
      std::string_view access_token, char *output, size_t capacity) override;
};

}  // namespace esphome::usp_spotify_auth
