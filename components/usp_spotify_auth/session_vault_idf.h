#pragma once
#include "session_vault.h"
#include "esp_memory_utils.h"
#include "nvs.h"
#include "esp_efuse.h"
#include "esp_efuse_chip.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include <array>
#include <atomic>
#include <cstring>

namespace usp_spotify_session {
constexpr char SESSION_NAMESPACE[]="usp_spotify";
constexpr char SESSION_RECORD_KEY[]="session_v1";
constexpr char LOCAL_KEY_RECORD_KEY[]="local_key_v1";
constexpr uint8_t LOCAL_KEY_ID=0xFF;
constexpr size_t LOCAL_KEY_BYTES=32;
enum class ProtectionMode : uint8_t { NONE, HARDWARE, LOCAL };
constexpr std::array<uint8_t, 32> KEY_DOMAIN = {
    0x55,0x53,0x50,0x2d,0x53,0x50,0x4f,0x54,0x49,0x46,0x59,0x2d,0x53,0x45,0x53,0x53,
    0x49,0x4f,0x4e,0x2d,0x56,0x41,0x55,0x4c,0x54,0x2d,0x56,0x31,0x00,0x00,0x00,0x01};

/** Private IDF NVS handle avoids the main-loop-only ESPHome preferences queue. */
class IdfStorage final : public Storage {
 public:
  IdfStorage()=default;
  IdfStorage(const IdfStorage &)=delete;
  IdfStorage &operator=(const IdfStorage &)=delete;
  ~IdfStorage() override { if(ready_)nvs_close(handle_); }

  bool setup() {
    if(ready_)return true;
    if(!internal_stack_()){error_.store(true);return false;}
    const auto result=nvs_open(SESSION_NAMESPACE,NVS_READWRITE,&handle_);
    ready_=result==ESP_OK;error_.store(!ready_);return ready_;
  }

  bool load(Record *record) override {
    if(!record || !can_access_())return false;
    size_t bytes=sizeof(*record);
    const auto result=nvs_get_blob(handle_,SESSION_RECORD_KEY,record,&bytes);
    const bool ok=result==ESP_OK && bytes==sizeof(*record);
    error_.store(!ok && result!=ESP_ERR_NVS_NOT_FOUND);
    return ok;
  }

  bool save(const Record &record) override {
    if(!can_access_())return false;
    auto result=nvs_set_blob(handle_,SESSION_RECORD_KEY,&record,sizeof(record));
    if(result==ESP_OK)result=nvs_commit(handle_);
    error_.store(result!=ESP_OK);return result==ESP_OK;
  }

  bool load_local_key(std::array<uint8_t,LOCAL_KEY_BYTES> *key,bool *found) {
    if(!key || !found || !can_access_())return false;
    key->fill(0);*found=false;size_t bytes=key->size();
    const auto result=nvs_get_blob(handle_,LOCAL_KEY_RECORD_KEY,key->data(),&bytes);
    if(result==ESP_ERR_NVS_NOT_FOUND){error_.store(false);return true;}
    const bool ok=result==ESP_OK && bytes==key->size();
    if(!ok)key->fill(0);else *found=true;
    error_.store(!ok);return ok;
  }

  bool save_local_key(const std::array<uint8_t,LOCAL_KEY_BYTES> &key) {
    if(!can_access_())return false;
    auto result=nvs_set_blob(handle_,LOCAL_KEY_RECORD_KEY,key.data(),key.size());
    if(result==ESP_OK)result=nvs_commit(handle_);
    error_.store(result!=ESP_OK);return result==ESP_OK;
  }

  bool io_error() const override { return error_.load(); }

 private:
  /** Reading flash can disable caches, so reject any external-stack caller. */
  static bool internal_stack_() {
    const uint8_t marker=0;
    return esp_ptr_internal(&marker);
  }
  bool can_access_() {
    const bool allowed=ready_ && internal_stack_();
    if(!allowed)error_.store(true);
    return allowed;
  }
  nvs_handle_t handle_{0};
  bool ready_{false};
  std::atomic<bool> error_{false};
};

class IdfCrypto final : public Crypto {
 public:
  bool setup(IdfStorage &storage,uint8_t hardware_key_id) {
    this->clear_local_key_();this->mode_=ProtectionMode::NONE;
    this->active_key_id_=hardware_key_id;
    if(this->hardware_key_state_(hardware_key_id)==KeyState::READY){
      this->mode_=ProtectionMode::HARDWARE;return true;
    }
    std::array<uint8_t,LOCAL_KEY_BYTES> candidate{};bool found=false;
    if(!storage.load_local_key(&candidate,&found))return false;
    if(!found){
      esp_fill_random(candidate.data(),candidate.size());
      if(this->all_zero_(candidate) || !storage.save_local_key(candidate)){
        wipe(candidate.data(),candidate.size());return false;
      }
    } else if(this->all_zero_(candidate)){
      wipe(candidate.data(),candidate.size());return false;
    }
    this->local_key_=candidate;wipe(candidate.data(),candidate.size());
    this->active_key_id_=LOCAL_KEY_ID;this->mode_=ProtectionMode::LOCAL;return true;
  }

  ProtectionMode mode() const { return this->mode_; }
  uint8_t active_key_id() const { return this->active_key_id_; }

  KeyState key_state(uint8_t key_id) override {
    if(key_id==LOCAL_KEY_ID)
      return this->mode_==ProtectionMode::LOCAL && !this->all_zero_(this->local_key_)
          ? KeyState::READY : KeyState::ERROR;
    return this->hardware_key_state_(key_id);
  }

  bool nonce(uint8_t *output,size_t size) override {
    if(!output || size!=NONCE_BYTES)return false;
    esp_fill_random(output,size);return true;
  }

  bool seal(uint8_t key_id,const uint8_t *nonce,const Header &header,
            const Plaintext &plain,Record *record) override {
    if(!record || !nonce || !this->derive_(key_id))return false;
    record->header=header;
    const bool ok=this->crypt_(true,nonce,header,
        reinterpret_cast<const uint8_t *>(&plain),record->ciphertext.data(),record->tag.data());
    this->clear_key_();return ok;
  }

  bool open(uint8_t key_id,const Record &record,Plaintext *plain) override {
    if(!plain || !this->derive_(key_id))return false;
    const bool ok=this->crypt_(false,record.nonce.data(),record.header,record.ciphertext.data(),
        reinterpret_cast<uint8_t *>(plain),const_cast<uint8_t *>(record.tag.data()));
    this->clear_key_();if(!ok)wipe(plain,sizeof(*plain));return ok;
  }

  ~IdfCrypto() override { this->clear_key_();this->clear_local_key_(); }

 private:
  KeyState hardware_key_state_(uint8_t key_id) const {
    if(key_id>=HMAC_KEY_MAX)return KeyState::ERROR;
    const auto block=static_cast<esp_efuse_block_t>(EFUSE_BLK_KEY0+key_id);
    if(esp_efuse_key_block_unused(block))return KeyState::UNPROVISIONED;
    if(esp_efuse_get_key_purpose(block)!=ESP_EFUSE_KEY_PURPOSE_HMAC_UP)
      return KeyState::WRONG_PURPOSE;
    if(!esp_efuse_get_key_dis_read(block) || !esp_efuse_get_key_dis_write(block) ||
       !esp_efuse_get_keypurpose_dis_write(block))return KeyState::ERROR;
    return KeyState::READY;
  }

  static bool all_zero_(const std::array<uint8_t,LOCAL_KEY_BYTES> &key) {
    uint8_t value=0;for(const auto byte:key)value|=byte;return value==0;
  }

  bool derive_(uint8_t key_id) {
    this->clear_key_();
    if(key_id==LOCAL_KEY_ID){
      if(this->key_state(key_id)!=KeyState::READY)return false;
      this->key_=this->local_key_;return true;
    }
    if(this->hardware_key_state_(key_id)!=KeyState::READY)return false;
    return esp_hmac_calculate(static_cast<hmac_key_id_t>(key_id),KEY_DOMAIN.data(),
                              KEY_DOMAIN.size(),this->key_.data())==ESP_OK;
  }

  bool set_key_(mbedtls_gcm_context *ctx) {
    return mbedtls_gcm_setkey(ctx,MBEDTLS_CIPHER_ID_AES,this->key_.data(),256)==0;
  }

  bool crypt_(bool encrypt,const uint8_t *nonce,const Header &header,
              const uint8_t *input,uint8_t *output,uint8_t *tag) {
    mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
    if(!this->set_key_(&ctx)){mbedtls_gcm_free(&ctx);return false;}
    const auto *aad=reinterpret_cast<const uint8_t *>(&header);
    const int rc=encrypt ? mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,
        sizeof(Plaintext),nonce,NONCE_BYTES,aad,sizeof(Header),input,output,TAG_BYTES,tag)
      : mbedtls_gcm_auth_decrypt(&ctx,sizeof(Plaintext),nonce,NONCE_BYTES,aad,sizeof(Header),
        tag,TAG_BYTES,input,output);
    mbedtls_gcm_free(&ctx);return rc==0;
  }

  void clear_key_(){wipe(this->key_.data(),this->key_.size());}
  void clear_local_key_(){wipe(this->local_key_.data(),this->local_key_.size());}
  std::array<uint8_t,32> key_{};
  std::array<uint8_t,LOCAL_KEY_BYTES> local_key_{};
  ProtectionMode mode_{ProtectionMode::NONE};
  uint8_t active_key_id_{0};
};}  // namespace usp_spotify_session
