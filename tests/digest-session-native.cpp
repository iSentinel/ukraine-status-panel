#include "digest_auth_state.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

using namespace esphome::web_server_idf::digest_auth;

/** Generate only synthetic test tokens, without panel credentials. */
static std::string token(char prefix, unsigned value) {
  char output[33];
  std::snprintf(output, sizeof(output), "%c%031x", prefix, value);
  return output;
}

/** Renew idle lifetime after a recent successful authenticated request. */
static void recent_activity_renews() {
  NonceStore store;
  const auto nonce = token('a', 1);
  const auto opaque = token('b', 1);
  store.issue(nonce.c_str(), opaque.c_str(), 100);
  assert(store.consume(nonce, opaque, 1, 101));
  assert(store.consume(nonce, opaque, 2, 100 + NONCE_TTL_MS - 1));
  assert(store.consume(nonce, opaque, 3, 100 + NONCE_TTL_MS + 1));
  std::cout << "N1_STATE_FIXED_COPY recent_authenticated_request_renews\n";
}

/** Pending challenges preserve an authenticated session. */
static void authenticated_session_protected() {
  NonceStore store;
  const auto nonce = token('a', 1);
  const auto opaque = token('b', 1);
  store.issue(nonce.c_str(), opaque.c_str(), 100);
  assert(store.consume(nonce, opaque, 1, 101));
  for (unsigned index = 0; index < NONCE_CAPACITY; ++index) {
    const auto other = token('c', index + 1);
    store.issue(other.c_str(), opaque.c_str(), 102 + index);
  }
  assert(store.consume(nonce, opaque, 2, 200));
  std::cout << "N2_STATE_FIXED_COPY unauthenticated_challenges_preserve_active_session\n";
}

/** Invalid or replayed requests cannot prolong a session. */
static void rejected_request_does_not_renew() {
  for (bool replay : {false, true}) {
    NonceStore store;
    const auto nonce = token('a', 2);
    const auto opaque = token('b', 2);
    store.issue(nonce.c_str(), opaque.c_str(), 100);
    assert(store.consume(nonce, opaque, 1, 101));
    const auto wrong = replay ? opaque : token('c', 2);
    assert(!store.consume(nonce, wrong, replay ? 1 : 2, NONCE_TTL_MS));
    assert(store.consume_result(nonce, opaque, 2, 101 + NONCE_TTL_MS)
           == NonceStatus::EXPIRED);
  }
}

/** Ten independently authenticated clients survive pending challenges. */
static void capacity_and_replacement() {
  NonceStore store;
  const auto opaque = token('b', 3);
  for (unsigned i = 0; i < 10; ++i) {
    const auto nonce = token('a', i);
    store.issue(nonce.c_str(), opaque.c_str(), 100 + i);
    assert(store.consume(nonce, opaque, 1, 100 + i));
  }
  for (unsigned i = 0; i < 32; ++i) {
    const auto pending = token('c', i);
    store.issue(pending.c_str(), opaque.c_str(), 120 + i);
  }
  for (unsigned i = 1; i < 10; ++i) {
    assert(store.consume(token('a', i), opaque, 2, 200 + i));
  }
  const auto newest = token('d', 1);
  store.issue(newest.c_str(), opaque.c_str(), 300);
  assert(store.consume(newest, opaque, 1, 300));
  assert(!store.consume(token('a', 0), opaque, 2, 301));
  for (unsigned i = 1; i < 10; ++i) {
    assert(store.consume(token('a', i), opaque, 3, 301 + i));
  }
}

/** Idle timeout arithmetic remains correct across the millisecond wrap. */
static void idle_boundary_and_wrap() {
  const uint32_t start = UINT32_MAX - NONCE_TTL_MS / 2;
  for (uint32_t delta : {NONCE_TTL_MS - 1, NONCE_TTL_MS}) {
    NonceStore store;
    const auto nonce = token('a', delta);
    const auto opaque = token('b', delta);
    store.issue(nonce.c_str(), opaque.c_str(), start);
    assert(store.consume(nonce, opaque, 1, start));
    const auto status = store.consume_result(nonce, opaque, 2, start + delta);
    assert(status == (delta < NONCE_TTL_MS ? NonceStatus::ACCEPTED
                                          : NonceStatus::EXPIRED));
  }
}

int main() {
  recent_activity_renews();
  authenticated_session_protected();
  rejected_request_does_not_renew();
  capacity_and_replacement();
  idle_boundary_and_wrap();
  std::cout << "DIGEST_SESSION_STATE=PASS cases5 protected10\n";
}
