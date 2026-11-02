#include "playback_worker_engine.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace usp_spotify_playback_worker;
using usp_spotify_playback::PlaybackSnapshot;
using usp_spotify_playback::ResponseKind;
using usp_spotify_tokens::HttpResult;

struct Clock final : WorkerClock {
  uint32_t now_ms{100};
  uint32_t now() const override { return now_ms; }
};
struct Bridge final : WorkerBridge {
  uint32_t epoch{1};
  std::vector<PlaybackState> states;
  uint32_t generation() const override { return epoch; }
  bool publish(uint32_t generation, const PlaybackState &state) override {
    if (generation != epoch) return false;
    states.push_back(state);
    return true;
  }
};
struct Transport final : WorkerTransport {
  std::vector<HttpResult> results;
  std::vector<std::string> bodies;
  size_t calls{0};
  std::string last_token;
  HttpResult perform(std::string_view token, char *out, size_t cap) override {
    ++calls;
    last_token.assign(token);
    assert(calls <= results.size() && calls <= bodies.size());
    auto result = results[calls - 1];
    const auto &body = bodies[calls - 1];
    assert(body.size() <= cap);
    if (!body.empty()) std::memcpy(out, body.data(), body.size());
    result.size = body.size();
    return result;
  }
};
struct Parser final : WorkerParser {
  bool accept{true};
  bool playing{true};
  bool parse(const char *body, size_t size, PlaybackSnapshot *out) override {
    if (!accept || !body || !out || size == 0) return false;
    *out = {};
    out->valid = true;
    out->has_item = true;
    out->is_playing = playing;
    std::strncpy(out->item_name.data(), "fixture track", out->item_name.size() - 1);
    return true;
  }
};

HttpResult response(int status, bool complete = true, uint32_t retry = 0) {
  HttpResult result{};
  result.attempted = true;
  result.complete = complete;
  result.status = status;
  if (retry) {
    result.retry_valid = true;
    result.retry_ms = retry;
  }
  return result;
}

void playing_and_statuses() {
  Clock clock;
  Bridge bridge;
  Transport transport;
  Parser parser;
  Worker worker;
  worker.step(bridge, "", transport, parser, clock);
  assert(transport.calls == 0 && !bridge.states.empty());
  assert(bridge.states.back().kind == ResponseKind::EMPTY);
  transport.results = {response(200), response(204), response(429, true, 120000),
                       response(401), response(403), response(503)};
  transport.bodies = {"{track}", "", "", "", "", ""};
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 1 && transport.last_token == "access_A");
  assert(bridge.states.back().kind == ResponseKind::JSON);
  clock.now_ms += 4999;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 1);
  clock.now_ms += 1;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 2 && bridge.states.back().kind == ResponseKind::EMPTY);
  clock.now_ms += 59999;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 2);
  clock.now_ms += 1;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 3 && bridge.states.back().kind == ResponseKind::RATE_LIMIT);
  clock.now_ms += 119999;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 3);
  clock.now_ms += 1;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 4 && bridge.states.back().kind == ResponseKind::REAUTH);
  assert(worker.take_reauth() && !worker.take_reauth());
  clock.now_ms += 5000;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 5 && bridge.states.back().kind == ResponseKind::FORBIDDEN);
  clock.now_ms += 30000;
  worker.step(bridge, "access_A", transport, parser, clock);
  assert(transport.calls == 6 && bridge.states.back().kind == ResponseKind::RETRY);
  bridge.epoch++;
  worker.step(bridge, "", transport, parser, clock);
  assert(transport.calls == 6 && bridge.states.back().kind == ResponseKind::EMPTY);
}

void paused_and_idle_cadence() {
  Clock clock;
  Bridge bridge;
  Transport transport;
  Parser parser;
  Worker worker;
  parser.playing = false;
  transport.results = {response(200), response(204), response(204)};
  transport.bodies = {"{paused}", "", ""};
  worker.step(bridge, "access_B", transport, parser, clock);
  assert(transport.calls == 1);
  clock.now_ms += 29999;
  worker.step(bridge, "access_B", transport, parser, clock);
  assert(transport.calls == 1);
  clock.now_ms += 1;
  worker.step(bridge, "access_B", transport, parser, clock);
  assert(transport.calls == 2);
  clock.now_ms += 59999;
  worker.step(bridge, "access_B", transport, parser, clock);
  assert(transport.calls == 2);
  clock.now_ms += 1;
  worker.step(bridge, "access_B", transport, parser, clock);
  assert(transport.calls == 3);
}

void wrap_and_malformed() {
  Worker worker;
  Bridge bridge;
  Transport transport;
  Parser parser;
  Clock clock;
  clock.now_ms = 0xFFFFFF00U;
  transport.results = {response(200), response(200)};
  transport.bodies = {"{ok}", "{bad}"};
  worker.step(bridge, "access_C", transport, parser, clock);
  assert(transport.calls == 1);
  clock.now_ms += 5000U;
  parser.accept = false;
  worker.step(bridge, "access_C", transport, parser, clock);
  assert(transport.calls == 2 && bridge.states.back().kind == ResponseKind::RETRY);
  assert(!bridge.states.back().snapshot.valid);
}

int main() {
  playing_and_statuses();
  paused_and_idle_cadence();
  wrap_and_malformed();
  std::cout << "SPOTIFY_PLAYBACK_WORKER_NATIVE=PASS adaptive5_30_60 429 401 403 "
               "5xx generation_clear wrap malformed no_network\n";
}
