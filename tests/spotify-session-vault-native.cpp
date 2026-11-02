#include "session_vault.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace usp_spotify_session;

class MemoryStorage final : public Storage {
 public:
  bool load(Record *record) override { if(!present)return false; *record=value; return true; }
  bool save(const Record &record) override { if(fail)return false; value=record;present=true;++writes;return true; }
  Record value{}; bool present{false},fail{false}; unsigned writes{0};
};

class FakeCrypto final : public Crypto {
 public:
  KeyState state{KeyState::READY}; uint32_t counter{1}; std::vector<std::array<uint8_t,NONCE_BYTES>> nonces;
  KeyState key_state(uint8_t) override {return state;}
  bool nonce(uint8_t *out,size_t size) override {
    if(size!=NONCE_BYTES)return false;
    std::array<uint8_t,NONCE_BYTES> value{};
    for(size_t i=0;i<size;++i)value[i]=uint8_t(counter+i*17U);
    ++counter;nonces.push_back(value);
    std::memcpy(out,value.data(),size);return true;
  }
  uint8_t mask(uint8_t key,const uint8_t *nonce,size_t at) const {return uint8_t(0xA5U^key^nonce[at%NONCE_BYTES]^uint8_t(at*29U));}
  void tag(const Record &record,std::array<uint8_t,TAG_BYTES> *out) const {
    out->fill(0);const auto *head=reinterpret_cast<const uint8_t *>(&record.header);
    for(size_t i=0;i<sizeof(Header);++i)(*out)[i%TAG_BYTES]^=uint8_t(head[i]+i);
    for(size_t i=0;i<record.nonce.size();++i)(*out)[i%TAG_BYTES]^=uint8_t(record.nonce[i]+3U*i);
    for(size_t i=0;i<record.ciphertext.size();++i)(*out)[i%TAG_BYTES]^=uint8_t(record.ciphertext[i]+i);
  }
  bool seal(uint8_t key,const uint8_t *nonce,const Header &header,const Plaintext &plain,Record *record) override {
    record->header=header;const auto *src=reinterpret_cast<const uint8_t *>(&plain);
    for(size_t i=0;i<record->ciphertext.size();++i)record->ciphertext[i]=src[i]^mask(key,nonce,i);
    tag(*record,&record->tag);return true;
  }
  bool open(uint8_t key,const Record &record,Plaintext *plain) override {
    std::array<uint8_t,TAG_BYTES> wanted{};tag(record,&wanted);if(wanted!=record.tag)return false;
    auto *dst=reinterpret_cast<uint8_t *>(plain);
    for(size_t i=0;i<record.ciphertext.size();++i)dst[i]=record.ciphertext[i]^mask(key,record.nonce.data(),i);
    return true;
  }
};

void basic_roundtrip() {
  MemoryStorage s;FakeCrypto c;Workspace workspace;Vault v(s,c,5);v.set_workspace(&workspace);Session out;
  assert(v.load(&out)==LoadState::EMPTY);
  assert(v.store("refresh+/secret","client123","user-read-playback-state user-modify-playback-state"));
  assert(v.load(&out)==LoadState::READY);
  assert(std::string(out.refresh.data())=="refresh+/secret" && std::string(out.client.data())=="client123");
  const std::string raw(reinterpret_cast<const char *>(&s.value),sizeof(s.value));
  assert(raw.find("refresh+/secret")==std::string::npos);
}
void tamper_and_keys() {
  MemoryStorage s;FakeCrypto c;Workspace workspace;Vault v(s,c,2);v.set_workspace(&workspace);Session out;assert(v.store("refresh","client123","scope"));
  s.value.ciphertext[77]^=1;assert(v.load(&out)==LoadState::CORRUPT && out.refresh[0]==0);s.value.ciphertext[77]^=1;
  s.value.tag[3]^=1;assert(v.load(&out)==LoadState::CORRUPT);s.value.tag[3]^=1;
  c.state=KeyState::UNPROVISIONED;assert(v.load(&out)==LoadState::KEY_UNAVAILABLE && !v.store("refresh2","client123",""));
  c.state=KeyState::WRONG_PURPOSE;assert(v.load(&out)==LoadState::KEY_UNAVAILABLE);
}
void rotation_clear_and_failures() {
  MemoryStorage s;FakeCrypto c;Workspace workspace;Vault v(s,c,1);v.set_workspace(&workspace);Session out;
  assert(v.store("refresh-one","client123","scope-a"));const auto first=s.value.nonce;
  assert(v.store("refresh-two","client123","scope-b"));assert(first!=s.value.nonce);
  assert(v.load(&out)==LoadState::READY && std::string(out.refresh.data())=="refresh-two");
  const Record before=s.value;s.fail=true;assert(!v.store("refresh-three","client123","scope-c"));assert(std::memcmp(&before,&s.value,sizeof(before))==0);
  s.fail=false;assert(v.clear() && v.load(&out)==LoadState::EMPTY && out.refresh[0]==0);
}
void bounds() {
  MemoryStorage s;FakeCrypto c;Workspace workspace;Vault v(s,c,0);v.set_workspace(&workspace);std::string huge(usp_spotify_tokens::MAX_TOKEN+1,'x');
  assert(!v.store(huge,"client123",""));assert(!v.store("refresh","bad client",""));
  std::string badscope(513,'s');assert(!v.store("refresh","client123",badscope));assert(s.writes==0);
}

/** Workspace owns every large temporary and is erased after every operation. */
void workspace_lifecycle() {
  MemoryStorage storage;FakeCrypto crypto;Vault vault(storage,crypto,5);Session out;
  assert(!vault.clear() && !vault.store("refresh","client123","scope"));
  assert(vault.load(&out)==LoadState::IO_ERROR && storage.writes==0);
  Workspace workspace;vault.set_workspace(&workspace);
  auto erased=[&](){
    const auto *bytes=reinterpret_cast<const uint8_t *>(&workspace);
    for(size_t i=0;i<sizeof(workspace);++i)assert(bytes[i]==0);
  };
  assert(vault.store("refresh","client123","scope"));erased();
  assert(vault.load(&out)==LoadState::READY);erased();
  assert(std::string(out.refresh.data())=="refresh");
  storage.fail=true;assert(!vault.clear());erased();
  storage.fail=false;assert(vault.clear());erased();
  assert(vault.load(&out)==LoadState::EMPTY);erased();
}

int main(){workspace_lifecycle();basic_roundtrip();tamper_and_keys();rotation_clear_and_failures();bounds();
  std::cout<<"SPOTIFY_SESSION_VAULT_NATIVE=PASS encrypted_blob tamper key_state nonce_rotation clear atomic_fail bounds no_access_token no_network\n";}
