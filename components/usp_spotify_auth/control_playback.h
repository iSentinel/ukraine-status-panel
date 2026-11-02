#pragma once
#include "control_coordinator.h"
#include "playback_worker_engine.h"

namespace usp_spotify_controls {
/** Only current, ordinary track/episode snapshots may permit command buttons. */
inline Facts playback_facts(const usp_spotify_playback_worker::PlaybackState &state) {
  const auto &snapshot=state.snapshot;
  const bool content=snapshot.type==usp_spotify_playback::ItemType::TRACK ||
      snapshot.type==usp_spotify_playback::ItemType::EPISODE;
  return {state.updated_at,snapshot.control_allowed_mask,
      state.kind==usp_spotify_playback::ResponseKind::JSON && snapshot.valid &&
      snapshot.control_actions_known && snapshot.has_item && content &&
      !snapshot.private_session,snapshot.device_active,snapshot.restricted,
      snapshot.is_playing};
}

/** Observe even a stale429 before the existing worker can discard its old snapshot. */
class ObservedPlaybackTransport final : public usp_spotify_playback_worker::WorkerTransport {
 public:
  ObservedPlaybackTransport(usp_spotify_playback_worker::WorkerTransport &transport,
      Coordinator &coordinator,const usp_spotify_playback_worker::WorkerClock &clock)
      : transport_(transport),coordinator_(coordinator),clock_(clock) {}
  usp_spotify_tokens::HttpResult perform(std::string_view token,char *output,
                                       size_t capacity) override {
    if(!coordinator_.api_ready(clock_.now()))return {};
    const auto response=transport_.perform(token,output,capacity);
    const uint32_t seconds=response.retry_ms/1000U+(response.retry_ms%1000U?1U:0U);
    coordinator_.provider_response(response.status,response.attempted,
        response.retry_valid,response.retry_excess,seconds,clock_.now());
    return response;
  }
 private:
  usp_spotify_playback_worker::WorkerTransport &transport_;
  Coordinator &coordinator_;
  const usp_spotify_playback_worker::WorkerClock &clock_;
};

/** Keep each GET attached to the token owner even if logout happens during HTTPS. */
class OwnedPlaybackBridge final : public usp_spotify_playback_worker::WorkerBridge {
 public:
  OwnedPlaybackBridge(usp_spotify_playback_worker::WorkerBridge &bridge,uint32_t generation)
      : bridge_(bridge),generation_(generation) {}
  uint32_t generation() const override { return generation_; }
  bool publish(uint32_t generation,
               const usp_spotify_playback_worker::PlaybackState &state) override {
    return generation==generation_ && bridge_.publish(generation,state);
  }
 private:
  usp_spotify_playback_worker::WorkerBridge &bridge_;
  uint32_t generation_;
};

/** GET observes a sent command; cooldown never schedules or replays a command. */
inline void poll_playback(Coordinator &coordinator,
    usp_spotify_playback_worker::Worker &worker,
    usp_spotify_playback_worker::WorkerBridge &bridge,std::string_view token,
    usp_spotify_playback_worker::WorkerTransport &transport,
    usp_spotify_playback_worker::WorkerParser &parser,
    const usp_spotify_playback_worker::WorkerClock &clock) {
  if(!coordinator.api_ready(clock.now()))return;
  if(coordinator.take_observation())worker.request_poll();
  worker.step(bridge,token,transport,parser,clock);
}
}  // namespace usp_spotify_controls
