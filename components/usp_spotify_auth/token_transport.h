#pragma once
#include "token_worker_engine.h"
namespace esphome::usp_spotify_auth {
/** Fixed HTTPS token endpoint; this transport is called only by the worker. */
class IdfTokenTransport final : public usp_spotify_tokens::Transport {
 public:
  static bool available();
  usp_spotify_tokens::HttpResult perform(const char *,size_t,char *,size_t) override;
};
}
