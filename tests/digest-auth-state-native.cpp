#include "components/web_server_idf/digest_auth_state.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

using esphome::web_server_idf::digest_auth::NONCE_CAPACITY;
using esphome::web_server_idf::digest_auth::NONCE_TTL_MS;
using esphome::web_server_idf::digest_auth::NonceStore;
using esphome::web_server_idf::digest_auth::parse_nonce_count;

static std::string token(char prefix, unsigned value) {
  char buffer[33];
  std::snprintf(buffer, sizeof(buffer), "%c%031x", prefix, value);
  return buffer;
}

int main() {
  uint32_t nc = 0;
  assert(parse_nonce_count("00000001", nc) && nc == 1);
  assert(parse_nonce_count("0000000A", nc) && nc == 10);
  assert(!parse_nonce_count("00000000", nc));
  assert(!parse_nonce_count("0000000g", nc));
  assert(!parse_nonce_count("000000001", nc));

  NonceStore store;
  const auto nonce = token('a', 1);
  const auto opaque = token('b', 1);
  store.issue(nonce.c_str(), opaque.c_str(), 100);
  assert(store.consume(nonce, opaque, 1, 101));
  assert(!store.consume(nonce, opaque, 1, 102));
  assert(store.consume(nonce, opaque, 3, 103));
  assert(store.consume(nonce, opaque, 2, 104));
  assert(!store.consume(nonce, opaque, 2, 105));
  assert(!store.consume(nonce, token('c', 1), 4, 106));

  NonceStore expiry;
  expiry.issue(nonce.c_str(), opaque.c_str(), 500);
  assert(expiry.consume(nonce, opaque, 1, 500 + NONCE_TTL_MS - 1));
  const auto nonce2 = token('a', 2);
  expiry.issue(nonce2.c_str(), opaque.c_str(), 700);
  assert(!expiry.consume(nonce2, opaque, 1, 700 + NONCE_TTL_MS));

  NonceStore rollover;
  const uint32_t issued = UINT32_MAX - 100U;
  rollover.issue(nonce.c_str(), opaque.c_str(), issued);
  assert(rollover.consume(nonce, opaque, 1, 50));

  NonceStore ring;
  std::string first_nonce;
  for (size_t i = 0; i < NONCE_CAPACITY + 1; i++) {
    const auto current = token('d', static_cast<unsigned>(i + 1));
    if (i == 0)
      first_nonce = current;
    ring.issue(current.c_str(), opaque.c_str(), static_cast<uint32_t>(i));
  }
  assert(!ring.consume(first_nonce, opaque, 1, 100));

  std::cout << "DIGEST_AUTH_STATE_NATIVE=PASS replay=blocked uri_state=bounded "
               "ttl=5min nc_window=64 rollover=1 ring=12\n";
}
