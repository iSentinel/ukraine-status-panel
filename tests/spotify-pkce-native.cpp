#include "usp_spotify_pkce.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

using namespace usp_spotify_pkce;

static uint8_t nibble(char ch) {
  if (ch >= '0' && ch <= '9') return uint8_t(ch - '0');
  if (ch >= 'a' && ch <= 'f') return uint8_t(10 + ch - 'a');
  assert(false);
  return 0;
}

template <size_t N>
static std::array<uint8_t, N> from_hex(std::string_view hex) {
  assert(hex.size() == N * 2U);
  std::array<uint8_t, N> bytes{};
  for (size_t i = 0; i < N; ++i)
    bytes[i] = uint8_t((nibble(hex[i * 2U]) << 4U) |
                       nibble(hex[i * 2U + 1U]));
  return bytes;
}

static void check_rfc7636() {
  const auto entropy = from_hex<VERIFIER_BYTES>(
      "7418dfb49799e0254ffa607dd8adbbba16d4254d69d6bff05b58055853848d79");
  const auto digest = from_hex<SHA256_BYTES>(
      "13d31e961a1ad8ec2f16b10c4c982e0876a878ad6df144566ee1894acb70f9c3");
  std::array<uint8_t, STATE_BYTES> state{};
  for (size_t i = 0; i < state.size(); ++i) state[i] = uint8_t(i + 1U);
  Session session;
  assert(begin_from_material(&session, 1000U, entropy, state, digest));
  assert(std::string(session.verifier.data()) ==
         "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
  assert(std::string(session.challenge.data()) ==
         "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  assert(std::strlen(session.state.data()) == STATE_CHARS);
}

static void check_callback_lifecycle() {
  const auto entropy = from_hex<VERIFIER_BYTES>(
      "7418dfb49799e0254ffa607dd8adbbba16d4254d69d6bff05b58055853848d79");
  const auto digest = from_hex<SHA256_BYTES>(
      "13d31e961a1ad8ec2f16b10c4c982e0876a878ad6df144566ee1894acb70f9c3");
  std::array<uint8_t, STATE_BYTES> state{};
  state.fill(0xA5U);
  Session session;
  Grant grant;
  assert(begin_from_material(&session, 500U, entropy, state, digest));
  const std::string expected_state = session.state.data();
  assert(consume_callback(&session, 501U, "wrong", true, false, &grant) ==
         CallbackResult::REJECT_STATE);
  assert(session.active && !grant.valid);
  assert(consume_callback(&session, 502U, expected_state, true, false, &grant) ==
         CallbackResult::ACCEPT_CODE);
  assert(!session.active && grant.valid);
  assert(std::string(grant.verifier.data()) ==
         "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
  assert(consume_callback(&session, 503U, expected_state, true, false) ==
         CallbackResult::REJECT_INACTIVE);
  grant.clear();
  assert(!grant.valid && grant.verifier[0] == '\0');
}

static void check_expiry_and_url() {
  const auto entropy = from_hex<VERIFIER_BYTES>(
      "7418dfb49799e0254ffa607dd8adbbba16d4254d69d6bff05b58055853848d79");
  const auto digest = from_hex<SHA256_BYTES>(
      "13d31e961a1ad8ec2f16b10c4c982e0876a878ad6df144566ee1894acb70f9c3");
  std::array<uint8_t, STATE_BYTES> state{};
  state.fill(0x5AU);
  Session session;
  const uint32_t start = 0xFFFFFF00U;
  assert(begin_from_material(&session, start, entropy, state, digest));
  const std::string expected_state = session.state.data();
  assert(consume_callback(&session, start + SESSION_TTL_MS, expected_state,
                          true, false) == CallbackResult::REJECT_EXPIRED);
  assert(!session.active);

  assert(begin_from_material(&session, 1U, entropy, state, digest));
  const std::string url = authorization_url(
      "0123456789abcdefABCDEF0123456789",
      "https://example.invalid/spotify-callback.html",
      "user-read-playback-state user-modify-playback-state", session);
  assert(url.find("response_type=code") != std::string::npos);
  assert(url.find("code_challenge_method=S256") != std::string::npos);
  assert(url.find("redirect_uri=https%3A%2F%2Fexample.invalid%2Fspotify-callback.html") != std::string::npos);
  assert(url.find("scope=user-read-playback-state%20user-modify-playback-state") != std::string::npos);
  assert(authorization_url("bad id!", "https://example.invalid/cb", "", session).empty());
  assert(authorization_url("abc123", "http://192.168.1.2/cb", "", session).empty());
  assert(authorization_url("abc123", "https://example.invalid/cb#fragment", "", session).empty());
}

int main() {
  check_rfc7636();
  check_callback_lifecycle();
  check_expiry_and_url();
  assert(constant_time_equal("same", "same"));
  assert(!constant_time_equal("same", "diff"));
  assert(!constant_time_equal("same", "same-longer"));
  std::cout << "SPOTIFY_PKCE_NATIVE=PASS rfc7636=1 s256=1 verifier43=1 "
               "state22=1 replay=blocked ttl5m_wrap=1 grant_zeroize=1 https_only=1\n";
}
