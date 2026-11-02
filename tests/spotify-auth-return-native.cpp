#include "usp_spotify_auth_return.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace usp_spotify_auth_return;
using usp_spotify_pkce::CallbackResult;

/** Deterministic material is confined to this host test, never the firmware RNG. */
usp_spotify_pkce::Session prepared(uint32_t now) {
  std::array<uint8_t, usp_spotify_pkce::VERIFIER_BYTES> entropy{};
  std::array<uint8_t, usp_spotify_pkce::STATE_BYTES> state{};
  std::array<uint8_t, usp_spotify_pkce::SHA256_BYTES> digest{};
  entropy.fill(0x21); state.fill(0x31); digest.fill(0x41);
  usp_spotify_pkce::Session session;
  assert(usp_spotify_pkce::begin_from_material(&session, now, entropy, state, digest));
  return session;
}
std::string start(Exchange &exchange, uint32_t now) {
  auto session = prepared(now);
  const std::string state = session.state.data();
  assert(exchange.adopt(&session, now, "client123", "https://callback.test/spotify/"));
  assert(!session.active && session.verifier[0] == 0);
  assert(exchange.status(now) == Status::AWAITING_USER);
  assert(exchange.authorization_url(now).find("code_challenge_method=S256") != std::string::npos);
  return state;
}

/** Real callback consumption and one-time handoff preserve opaque code bytes. */
void one_time() {
  Exchange exchange; TokenRequest request;
  const auto state = start(exchange, 100);
  assert(!exchange.take(101, &request));
  assert(exchange.accept(102, "wrong", "code", "") == CallbackResult::REJECT_STATE);
  assert(exchange.status(102) == Status::AWAITING_USER);
  const std::string code = "opaque+/%&= code";
  assert(exchange.accept(103, state, code, "") == CallbackResult::ACCEPT_CODE);
  assert(exchange.status(103) == Status::CODE_PENDING);
  assert(exchange.accept(104, state, "replay", "") == CallbackResult::REJECT_INACTIVE);
  assert(!exchange.take(105, nullptr) && exchange.status(105) == Status::CODE_PENDING);
  assert(exchange.take(106, &request));
  assert(std::string(request.code.data()) == code);
  assert(std::strlen(request.verifier.data()) == 43);
  assert(std::string(request.client_id.data()) == "client123");
  assert(std::string(request.redirect_uri.data()) == "https://callback.test/spotify/");
  assert(!exchange.take(107, &request));
  for (char ch : request.code) assert(ch == 0);
  for (char ch : request.verifier) assert(ch == 0);
}

/** Two deadlines remain exact across millis wrap; expired requests are erased. */
void deadlines() {
  Exchange exchange; TokenRequest request;
  const uint32_t begin = UINT32_MAX - 1000;
  auto state = start(exchange, begin);
  assert(exchange.status(begin + usp_spotify_pkce::SESSION_TTL_MS - 1) == Status::AWAITING_USER);
  assert(exchange.status(begin + usp_spotify_pkce::SESSION_TTL_MS) == Status::IDLE);
  assert(exchange.accept(begin + usp_spotify_pkce::SESSION_TTL_MS, state, "late", "") == CallbackResult::REJECT_INACTIVE);
  state = start(exchange, begin);
  assert(exchange.accept(begin + 1, state, "code", "") == CallbackResult::ACCEPT_CODE);
  assert(exchange.status(begin + GRANT_TTL_MS) == Status::CODE_PENDING);
  assert(!exchange.take(begin + 1 + GRANT_TTL_MS, &request));
  assert(exchange.status(begin + 1 + GRANT_TTL_MS) == Status::IDLE);
}

/** Matching malformed callbacks end a session; wrong-state input cannot end it. */
void malformed() {
  for (int mode = 0; mode < 5; ++mode) {
    Exchange exchange; const auto state = start(exchange, 0);
    std::string code = "code", error;
    if (mode == 0) code = std::string(MAX_CODE + 1, 'x');
    if (mode == 1) code = std::string("bad\0code", 8);
    if (mode == 2) error = "access_denied";
    if (mode == 3) { code.clear(); error = std::string(MAX_ERROR + 1, 'x'); }
    if (mode == 4) code.clear();
    assert(exchange.accept(1, state, code, error) == CallbackResult::REJECT_MALFORMED);
    assert(exchange.status(1) == Status::IDLE);
  }
  Exchange exchange; auto state = start(exchange, 10);
  assert(exchange.accept(11, state, "", "access_denied") == CallbackResult::ACCEPT_ERROR);
  assert(exchange.status(11) == Status::IDLE);
}

/** A second begin cannot replace a live session or overwrite a pending code. */
void busy_cancel() {
  Exchange exchange; auto state = start(exchange, 0);
  auto next = prepared(1);
  assert(!exchange.adopt(&next, 1, "client123", "https://callback.test/"));
  assert(!next.active && next.verifier[0] == 0);
  assert(exchange.accept(2, state, "code", "") == CallbackResult::ACCEPT_CODE);
  next = prepared(3);
  assert(!exchange.adopt(&next, 3, "client123", "https://callback.test/"));
  exchange.cancel();
  assert(exchange.status(4) == Status::IDLE);
  state = start(exchange, 5);
  assert(exchange.accept(6, state, "new", "") == CallbackResult::ACCEPT_CODE);
}

/** Configuration bounds and generic array copies fail without truncation. */
void configuration() {
  for (const auto *uri : {"http://192.168.1.2/", "https://", "https://?x",
       "https://a@b/cb", "https://a.test:0/cb", "https://a.test:65536/cb",
       "https://a..test/cb", "https://-a.test/cb", "https://a.test/cb?x=1",
       "https://a.test/cb#x", "https://a.test:abc/cb"}) {
    Exchange exchange; auto session = prepared(0);
    assert(!exchange.adopt(&session, 0, "client123", uri));
    assert(!session.active);
  }
  assert(valid_redirect("https://callback.test:8443/spotify/"));
  Exchange exchange; auto session = prepared(0);
  assert(!exchange.adopt(&session, 0, "bad client", "https://callback.test/"));
  session = prepared(0);
  assert(!exchange.adopt(&session, usp_spotify_pkce::SESSION_TTL_MS,
                         "client", "https://callback.test/"));
  std::array<char, 4> tiny{};
  assert(!copy_field(tiny, "four"));
  for (char ch : tiny) assert(ch == 0);
  assert(copy_field(tiny, "abc") && tiny[3] == 0);
}
int main() {
  one_time(); deadlines(); malformed(); busy_cancel(); configuration();
  std::cout << "SPOTIFY_AUTH_RETURN_NATIVE=PASS actual_PKCE inbox_once TTL60s session5m wrap opaque_code malformed replay cancel config zeroize no_network\n";
}
