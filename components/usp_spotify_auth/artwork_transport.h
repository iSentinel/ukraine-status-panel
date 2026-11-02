#pragma once
#include "artwork_http_body.h"
#include "artwork_request_plan.h"

namespace esphome::usp_spotify_auth {
struct ArtworkResult {
  int status{0};
  size_t size{0};
  bool attempted{false};
  bool complete{false};
  bool cancelled{false};
  usp_spotify_artwork_http::Cooldown retry{};
};
/** CDN-wide rate gate survives album and session changes in this instance. */
struct ArtworkRateGate {
  uint32_t started{0};
  uint32_t delay{0};
  bool blocked{false};
  bool allows(uint32_t now) {
    if (blocked || uint32_t(now - started) < delay) return false;
    delay = 0;
    return true;
  }
  void observe(const ArtworkResult &result, uint32_t now) {
    if (result.status != 429) return;
    started = now;
    delay = result.retry.delay;
    blocked = result.retry.blocked;
  }
};
/** Synchronous worker-only adapter; no tokens, pixels or LVGL are accepted. */
class IdfArtworkTransport {
 public:
  static bool available();
  ArtworkResult perform(const usp_spotify_artwork::Request &request,
      uint8_t *output, size_t capacity,
      bool (*current)(void *) = nullptr, void *owner = nullptr);
 private:
  ArtworkRateGate rate_{};
};
}  // namespace esphome::usp_spotify_auth
