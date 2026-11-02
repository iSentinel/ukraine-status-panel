#include "session_vault_idf.h"
#include <cassert>
#include <iostream>
using namespace usp_spotify_session;

/** A forbidden external-stack caller must never reach an NVS API. */
void reject_external_stack() {
  nvs_fixture::reset();
  host_flash_internal_stack=false;
  {
    IdfStorage storage;
    assert(!storage.setup() && nvs_fixture::opens==0);
    host_flash_internal_stack=true;assert(storage.setup());
    Record value{};const auto reads=nvs_fixture::reads,writes=nvs_fixture::writes;
    host_flash_internal_stack=false;
    assert(!storage.load(&value) && !storage.save(value) && storage.io_error());
    assert(nvs_fixture::reads==reads && nvs_fixture::writes==writes);
    host_flash_internal_stack=true;
  }
}

/** Only the private namespace is touched; size and storage errors remain explicit. */
void private_storage_errors() {
  nvs_fixture::reset();
  nvs_fixture::data["ordinary_preferences/region"]={31,0,0,0};
  const auto original=nvs_fixture::data["ordinary_preferences/region"];
  {
    IdfStorage storage;assert(storage.setup());Record value{};
    assert(!storage.load(&value) && !storage.io_error());
    value.header.key_id=5;
    assert(storage.save(value) && nvs_fixture::commits==1);
    Record copy{};assert(storage.load(&copy) && !storage.io_error());
    assert(std::memcmp(&value,&copy,sizeof(value))==0);
    nvs_fixture::fail_read=true;
    assert(!storage.load(&copy) && storage.io_error());
    nvs_fixture::fail_read=false;
    nvs_fixture::data["usp_spotify/session_v1"].push_back(0);
    assert(!storage.load(&copy) && storage.io_error());
    nvs_fixture::fail_write=true;const auto commits=nvs_fixture::commits;
    assert(!storage.save(value) && storage.io_error() && nvs_fixture::commits==commits);
    nvs_fixture::fail_write=false;nvs_fixture::fail_commit=true;
    assert(!storage.save(value) && storage.io_error());
    nvs_fixture::fail_commit=false;assert(storage.save(value) && !storage.io_error());
  }
  assert(nvs_fixture::data["ordinary_preferences/region"]==original);
  assert(nvs_fixture::closes==1);
}

int main() {
  reject_external_stack();private_storage_errors();
  std::cout<<"SPOTIFY_FLASH_STACK_NATIVE=PASS internal_guard blocks_external_before_NVS private_namespace size_errors commit_failure preserves_preferences no_device\n";
}
