#include "session_vault_idf.h"
#include "session_coordinator.h"
#include "spotify_flash_host/fake-vault-crypto.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace usp_spotify_session;

/** The actual vault/coordinator/storage path keeps stale generations out of NVS. */
void integrated_lifecycle() {
  nvs_fixture::reset();
  nvs_fixture::data["ordinary_preferences/region"]={31};
  IdfStorage storage;assert(storage.setup());FakeCrypto crypto;
  Workspace workspace;Vault vault(storage,crypto,5);vault.set_workspace(&workspace);
  std::atomic<uint32_t> generation{7};Coordinator<Vault> coordinator(vault,generation);
  assert(coordinator.configure("client123"));
  assert(coordinator.save(7,"refresh_fixture","client123","scope"));
  Session restored;assert(coordinator.load(7,&restored)==LoadState::READY);
  assert(std::string(restored.refresh.data())=="refresh_fixture");
  const auto &blob=nvs_fixture::data.at("usp_spotify/session_v1");
  assert(std::string(blob.begin(),blob.end()).find("refresh_fixture")==std::string::npos);
  uint32_t next=0;assert(coordinator.clear_then_advance(&next) && next==8);
  const auto writes=nvs_fixture::writes;
  assert(!coordinator.save(7,"late_refresh","client123","scope"));
  assert(nvs_fixture::writes==writes && coordinator.load(8,&restored)==LoadState::EMPTY);
  assert(nvs_fixture::data.at("ordinary_preferences/region")==std::vector<uint8_t>{31});
}

/** Concurrent calls share one heap workspace only through the real coordinator lock. */
void serialized_workspace() {
  nvs_fixture::reset();IdfStorage storage;assert(storage.setup());
  FakeCrypto crypto;Workspace workspace;Vault vault(storage,crypto,5);vault.set_workspace(&workspace);
  std::atomic<uint32_t> generation{1};Coordinator<Vault> coordinator(vault,generation);
  assert(coordinator.configure("client123"));
  std::vector<std::thread> workers;
  for(unsigned thread=0;thread<4;++thread)workers.emplace_back([&,thread](){
    for(unsigned i=0;i<1000;++i) {
      const auto token="fixture_"+std::to_string(thread)+"_"+std::to_string(i);
      assert(coordinator.save(1,token,"client123","scope"));
      Session restored;assert(coordinator.load(1,&restored)==LoadState::READY);
      assert(std::string(restored.refresh.data()).rfind("fixture_",0)==0);
    }
  });
  for(auto &thread:workers)thread.join();
  const auto *bytes=reinterpret_cast<const uint8_t *>(&workspace);
  for(size_t i=0;i<sizeof(workspace);++i)assert(bytes[i]==0);
  assert(nvs_fixture::writes==4000 && nvs_fixture::commits==4000);
}

int main() {
  integrated_lifecycle();serialized_workspace();
  std::cout<<"SPOTIFY_NVS_COORDINATOR_NATIVE=PASS actual_vault_storage_coordinator stale_generation logout ciphertext_only preferences_untouched serialized4000 workspace_zeroed no_network\n";
}
