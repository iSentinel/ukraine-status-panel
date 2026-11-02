#pragma once
#include "session_vault.h"
#include <cassert>
#include <vector>
using namespace usp_spotify_session;
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
