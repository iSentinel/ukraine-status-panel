#pragma once
#include "session_persistence.h"
#include "usp_spotify_auth_return.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string_view>

namespace usp_spotify_session {
/**
 * Serializes vault I/O between the HTTP/main task and the token worker.
 * Explicit BEGIN/CANCEL clears persisted state before advancing generation.
 * A stale worker can therefore never write after a successful transition.
 */
template<class VaultType> class Coordinator final : public PersistenceBridge {
 public:
  Coordinator(VaultType &vault,std::atomic<uint32_t> &generation)
      : vault_(vault),generation_(generation) {}
  Coordinator(const Coordinator &)=delete;
  Coordinator &operator=(const Coordinator &)=delete;

  bool configure(std::string_view client) {
    if(!usp_spotify_pkce::valid_client_id(client))return false;
    application_client_.fill(0);
    return usp_spotify_auth_return::copy_field(application_client_,client);
  }

  LoadState load(uint32_t generation,Session *output) override {
    if(!output)return LoadState::IO_ERROR;
    std::lock_guard<std::mutex> lock(mutex_);
    if(!current_(generation))return LoadState::IO_ERROR;
    const auto result=vault_.load(output);
    if(result!=LoadState::READY)return result;
    if(std::string_view(output->client.data())!=std::string_view(application_client_.data())) {
      output->clear();
      if(!vault_.clear())return LoadState::IO_ERROR;
      return LoadState::CORRUPT;
    }
    return LoadState::READY;
  }

  bool save(uint32_t generation,std::string_view refresh,
            std::string_view client,std::string_view scope) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_(generation) && client==std::string_view(application_client_.data()) &&
        vault_.store(refresh,client,scope);
  }

  bool clear(uint32_t generation) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_(generation) && vault_.clear();
  }

  /** HTTP callback is never allowed to wait behind worker flash/crypto work. */
  bool clear_then_advance(uint32_t *next_generation) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !vault_.clear())return false;
    const auto next=generation_.fetch_add(1,std::memory_order_acq_rel)+1U;
    if(next_generation)*next_generation=next;
    return true;
  }

 private:
  bool current_(uint32_t generation) const {
    return generation==generation_.load(std::memory_order_acquire);
  }
  VaultType &vault_;
  std::atomic<uint32_t> &generation_;
  std::mutex mutex_;
  std::array<char,usp_spotify_pkce::MAX_CLIENT_ID+1> application_client_{};
};
}  // namespace usp_spotify_session
