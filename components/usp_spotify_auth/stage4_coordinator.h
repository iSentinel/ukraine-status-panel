#pragma once
#include "control_coordinator.h"
#include "device_transfer_plan.h"
#include "value_control_policy.h"
#include "mode_control_policy.h"
#include "queue_policy.h"
#include <array>
#include <cstring>
#include <mutex>
#include <string_view>

namespace usp_spotify_stage4_binding {
enum class Family : uint8_t { NONE, DEVICE, VALUE, MODE, QUEUE };

/**
 * Main submits through this wrapper; worker dispatches the retained family once.
 * The existing basic Coordinator remains the single owner of Player Retry-After.
 */
class Coordinator {
 public:
  explicit Coordinator(usp_spotify_controls::Coordinator &player) : player_(player) {}
  Coordinator(const Coordinator &)=delete;
  Coordinator &operator=(const Coordinator &)=delete;

  void session(uint32_t generation,std::string_view scope) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (initialized_ && generation==generation_ && scope==scope_.data()) return;
    std::array<char,513> copied{};
    if (scope.size() >= copied.size()) scope={};
    if (!scope.empty()) std::memcpy(copied.data(),scope.data(),scope.size());
    generation_=generation;scope_=copied;initialized_=true;
    if(family_!=Family::NONE && !in_flight_) family_=Family::NONE;
    devices_.session(generation,scope);
    lock.unlock();
    value_.session(generation,scope);
    modes_.session(generation,scope);
    queue_.session(generation,scope);
  }

  void device_catalog(uint32_t generation,const usp_spotify_devices::Catalog &catalog,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.install(generation,catalog,now);
  }
  void transfer_action(uint32_t generation,bool known,bool allowed) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.transfer_action(generation,known,allowed);
  }
  void invalidate_devices(uint32_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!initialized_ || generation!=generation_)return;
    devices_.session(generation,scope_.data());
    if(family_==Family::DEVICE && !in_flight_)family_=Family::NONE;
  }
  void value_facts(uint32_t generation,const usp_spotify_value_controls::Facts &facts) {
    value_.facts(generation,facts);
  }
  void mode_facts(uint32_t generation,const usp_spotify_modes::Facts &facts) {
    modes_.facts(generation,facts);
  }
  void queue_facts(uint32_t generation,const usp_spotify_queue::Facts &facts) {
    queue_.facts(generation,facts);
  }

  bool api_ready(uint32_t now) { return player_.api_ready(now); }
  void provider_response(int status,bool attempted,bool known,bool excessive,uint32_t seconds,uint32_t now) {
    player_.provider_response(status,attempted,known,excessive,seconds,now);
  }

  uint64_t request_basic(uint32_t generation,usp_spotify_controls::Command command,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::NONE)return 0;
    return player_.request(generation,command,now);
  }
  uint64_t request_play_pause(uint32_t generation,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::NONE)return 0;
    return player_.request_play_pause(generation,now);
  }

  uint64_t request_transfer(uint32_t generation,uint8_t index,uint32_t revision,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now) || revision!=devices_.revision())return 0;
    const auto ticket=devices_.request(generation,index,now);
    if(ticket)family_=Family::DEVICE;
    return ticket;
  }
  uint32_t device_revision() const {
    std::lock_guard<std::mutex> lock(mutex_);return devices_.revision();
  }
  uint64_t request_volume(uint32_t generation,uint32_t value,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now))return 0;
    const auto ticket=value_.request_volume(generation,value,now);
    if(ticket)family_=Family::VALUE;
    return ticket;
  }
  uint64_t request_seek(uint32_t generation,uint32_t value,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now))return 0;
    const auto ticket=value_.request_seek(generation,value,now);
    if(ticket)family_=Family::VALUE;
    return ticket;
  }
  uint64_t request_shuffle(uint32_t generation,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now))return 0;
    const auto ticket=modes_.request_shuffle_toggle(generation,now);
    if(ticket)family_=Family::MODE;
    return ticket;
  }
  uint64_t request_repeat(uint32_t generation,usp_spotify_modes::Repeat mode,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now))return 0;
    const auto ticket=modes_.request_repeat(generation,mode,now);
    if(ticket)family_=Family::MODE;
    return ticket;
  }
  uint64_t request_queue_add(uint32_t generation,std::string_view uri,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!admit_(generation,now))return 0;
    const auto ticket=queue_.request(generation,uri,now);
    if(ticket)family_=Family::QUEUE;
    return ticket;
  }

  bool take_transfer(uint32_t generation,uint32_t now,usp_spotify_devices::TransferRequest *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::DEVICE || !player_.api_ready(now) || !devices_.take(generation,now,out))
      return reject_take_(Family::DEVICE);
    in_flight_=true;return true;
  }
  bool take_value(uint32_t generation,uint32_t now,usp_spotify_value_controls::Request *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::VALUE || !player_.api_ready(now) || !value_.take(generation,now,out))
      return reject_take_(Family::VALUE);
    in_flight_=true;return true;
  }
  bool take_mode(uint32_t generation,uint32_t now,usp_spotify_modes::Request *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::MODE || !player_.api_ready(now) || !modes_.take(generation,now,out))
      return reject_take_(Family::MODE);
    in_flight_=true;return true;
  }
  bool take_queue(uint32_t generation,uint32_t now,usp_spotify_queue::AddRequest *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(family_!=Family::QUEUE || !player_.api_ready(now) || !queue_.take(generation,now,out))
      return reject_take_(Family::QUEUE);
    in_flight_=true;return true;
  }

  bool current(uint32_t generation,const usp_spotify_devices::TransferRequest &r,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);return family_==Family::DEVICE&&in_flight_&&player_.api_ready(now)&&devices_.current(generation,r,now);
  }
  bool current(uint32_t generation,const usp_spotify_value_controls::Request &r,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);return family_==Family::VALUE&&in_flight_&&player_.api_ready(now)&&value_.current(generation,r,now);
  }
  bool current(uint32_t generation,const usp_spotify_modes::Request &r,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);return family_==Family::MODE&&in_flight_&&player_.api_ready(now)&&modes_.current(generation,r,now);
  }
  bool current(uint32_t generation,const usp_spotify_queue::AddRequest &r,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);return family_==Family::QUEUE&&in_flight_&&player_.api_ready(now)&&queue_.current(generation,r,now);
  }

  bool finish(uint32_t generation,const usp_spotify_devices::TransferRequest &r,int status,bool complete,
              bool attempted,bool retry_known,bool retry_excessive,uint32_t retry_seconds,uint32_t now) {
    player_.provider_response(status,attempted,retry_known,retry_excessive,retry_seconds,now);
    std::lock_guard<std::mutex> lock(mutex_);const bool ok=devices_.finish(r,status,complete);release_(Family::DEVICE);return ok&&generation==generation_;
  }
  bool finish(uint32_t generation,const usp_spotify_value_controls::Request &r,int status,bool complete,
              bool attempted,bool retry_known,bool retry_excessive,uint32_t retry_seconds,uint32_t now) {
    player_.provider_response(status,attempted,retry_known,retry_excessive,retry_seconds,now);
    std::lock_guard<std::mutex> lock(mutex_);const bool ok=value_.finish(r,status,complete);release_(Family::VALUE);return ok&&generation==generation_;
  }
  bool finish(uint32_t generation,const usp_spotify_modes::Request &r,int status,bool complete,
              bool attempted,bool retry_known,bool retry_excessive,uint32_t retry_seconds,uint32_t now) {
    player_.provider_response(status,attempted,retry_known,retry_excessive,retry_seconds,now);
    std::lock_guard<std::mutex> lock(mutex_);const bool ok=modes_.finish(r,status,complete);release_(Family::MODE);return ok&&generation==generation_;
  }
  bool finish(uint32_t generation,const usp_spotify_queue::AddRequest &r,int status,bool complete,
              bool attempted,bool retry_known,bool retry_excessive,uint32_t retry_seconds,uint32_t now) {
    player_.provider_response(status,attempted,retry_known,retry_excessive,retry_seconds,now);
    std::lock_guard<std::mutex> lock(mutex_);const bool ok=queue_.finish(r,status,complete);release_(Family::QUEUE);return ok&&generation==generation_;
  }

  Family family() const { std::lock_guard<std::mutex> lock(mutex_);return family_; }

 private:
  bool basic_idle_(uint32_t generation,uint32_t now) {
    usp_spotify_controls::ControlView view{};
    return player_.copy_view(generation,now,&view) && view.phase==usp_spotify_controls::Phase::IDLE;
  }
  bool admit_(uint32_t generation,uint32_t now) {
    return family_==Family::NONE && player_.api_ready(now) && basic_idle_(generation,now);
  }
  bool reject_take_(Family expected) {
    if(family_==expected && !in_flight_)family_=Family::NONE;
    return false;
  }
  void release_(Family expected) {
    if(family_==expected){family_=Family::NONE;in_flight_=false;}
  }
  usp_spotify_controls::Coordinator &player_;
  mutable std::mutex mutex_;
  usp_spotify_devices::TransferPlan devices_;
  usp_spotify_value_controls::Plan value_;
  usp_spotify_modes::Plan modes_;
  usp_spotify_queue::AddPlan queue_;
  std::array<char,513> scope_{};
  uint32_t generation_{0};
  Family family_{Family::NONE};
  bool in_flight_{false},initialized_{false};
};
}  // namespace usp_spotify_stage4_binding
