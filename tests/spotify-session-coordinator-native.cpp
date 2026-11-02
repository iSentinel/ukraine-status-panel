#include "session_coordinator.h"
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <thread>
using namespace usp_spotify_session;

class FakeVault {
 public:
  bool store(std::string_view refresh,std::string_view client,std::string_view scope) {
    if(fail_store)return false;
    if(block_store){
      std::unique_lock<std::mutex> lock(block_mutex);entered=true;changed.notify_all();
      changed.wait(lock,[&](){return released;});
    }
    session.clear();usp_spotify_auth_return::copy_field(session.refresh,refresh);
    usp_spotify_auth_return::copy_field(session.client,client);usp_spotify_auth_return::copy_field(session.scope,scope);
    present=true;++stores;return true;
  }
  LoadState load(Session *out) {
    ++loads;if(!present)return LoadState::EMPTY;
    out->clear();std::memcpy(out->refresh.data(),session.refresh.data(),session.refresh.size());
    std::memcpy(out->client.data(),session.client.data(),session.client.size());
    std::memcpy(out->scope.data(),session.scope.data(),session.scope.size());return LoadState::READY;
  }
  bool clear(){if(fail_clear)return false;session.clear();present=false;++clears;return true;}
  void seed(const char *client="client123") {store("refresh",client,"scope");stores=0;}
  Session session;bool present{false},fail_store{false},fail_clear{false},block_store{false};unsigned stores{0},loads{0},clears{0};
  std::mutex block_mutex;std::condition_variable changed;bool entered{false},released{false};
};

void basic_and_client_binding(){
  std::atomic<uint32_t> generation{7};FakeVault vault;Coordinator<FakeVault> c(vault,generation);assert(c.configure("client123"));
  assert(c.save(7,"refresh_A","client123","scope_A"));Session out;assert(c.load(7,&out)==LoadState::READY);
  assert(std::string(out.refresh.data())=="refresh_A");assert(!c.save(6,"old","client123",""));
  vault.seed("otherClient");assert(c.load(7,&out)==LoadState::CORRUPT&&!vault.present);
}
void transition_before_stale_save(){
  std::atomic<uint32_t> generation{1};FakeVault vault;vault.seed();Coordinator<FakeVault> c(vault,generation);assert(c.configure("client123"));
  uint32_t next=0;assert(c.clear_then_advance(&next)&&next==2&&!vault.present);
  assert(!c.save(1,"stale_refresh","client123","scope"));assert(!vault.present&&vault.stores==0);
}
void busy_transition_is_not_claimed(){
  std::atomic<uint32_t> generation{3};FakeVault vault;Coordinator<FakeVault> c(vault,generation);assert(c.configure("client123"));
  vault.block_store=true;bool saved=false;
  std::thread worker([&](){saved=c.save(3,"refresh_worker","client123","scope");});
  {std::unique_lock<std::mutex> lock(vault.block_mutex);assert(vault.changed.wait_for(lock,std::chrono::seconds(2),[&](){return vault.entered;}));}
  uint32_t next=0;assert(!c.clear_then_advance(&next)&&generation.load()==3);
  {std::lock_guard<std::mutex> lock(vault.block_mutex);vault.released=true;}vault.changed.notify_all();worker.join();assert(saved&&vault.present);
  assert(c.clear_then_advance(&next)&&next==4&&!vault.present);
}
void clear_failure_preserves_generation(){
  std::atomic<uint32_t> generation{9};FakeVault vault;vault.seed();Coordinator<FakeVault> c(vault,generation);assert(c.configure("client123"));
  vault.fail_clear=true;uint32_t next=0;assert(!c.clear_then_advance(&next)&&generation.load()==9&&vault.present);
  vault.fail_clear=false;assert(c.clear_then_advance(&next)&&generation.load()==10&&!vault.present);
}
int main(){basic_and_client_binding();transition_before_stale_save();busy_transition_is_not_claimed();clear_failure_preserves_generation();
  std::cout<<"SPOTIFY_SESSION_COORDINATOR_NATIVE=PASS client_bound stale_save_rejected clear_before_generation busy503 clear_fail_no_logout no_network\n";}
