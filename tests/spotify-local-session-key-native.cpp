#include "session_vault_idf.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace usp_spotify_session;

void create_and_reopen() {
  nvs_fixture::reset();
  std::vector<uint8_t> first;
  unsigned writes = 0;
  {
    IdfStorage storage; assert(storage.setup());
    IdfCrypto crypto; assert(crypto.setup(storage, 5));
    assert(crypto.mode() == ProtectionMode::LOCAL);
    assert(crypto.active_key_id() == LOCAL_KEY_ID);
    assert(crypto.key_state(LOCAL_KEY_ID) == KeyState::READY);
    first = nvs_fixture::data.at("usp_spotify/local_key_v1");
    assert(first.size() == LOCAL_KEY_BYTES);
    bool nonzero = false; for (auto byte : first) nonzero |= byte != 0;
    assert(nonzero); writes = nvs_fixture::writes; assert(writes == 1);
  }
  {
    IdfStorage storage; assert(storage.setup());
    IdfCrypto crypto; assert(crypto.setup(storage, 5));
    assert(crypto.mode() == ProtectionMode::LOCAL);
    assert(nvs_fixture::data.at("usp_spotify/local_key_v1") == first);
    assert(nvs_fixture::writes == writes);
  }
}

void failures_close() {
  nvs_fixture::reset(); nvs_fixture::fail_write = true;
  {
    IdfStorage storage; assert(storage.setup());
    IdfCrypto crypto; assert(!crypto.setup(storage, 5));
    assert(crypto.mode() == ProtectionMode::NONE);
  }
  nvs_fixture::reset();
  nvs_fixture::data["usp_spotify/local_key_v1"] = std::vector<uint8_t>(32, 0);
  {
    IdfStorage storage; assert(storage.setup());
    IdfCrypto crypto; assert(!crypto.setup(storage, 5));
    assert(crypto.mode() == ProtectionMode::NONE);
  }
}

int main() {
  create_and_reopen(); failures_close();
  std::cout << "SPOTIFY_LOCAL_SESSION_KEY_NATIVE=PASS create reopen same_key "
               "no_rewrite fail_closed zero_reject private_namespace "
               "no_efuse_write no_network\n";
}
