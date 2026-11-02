#include "spotify_auth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_http_client.h"
#include <cassert>
#include <condition_variable>
#include <iostream>
using ProductionApi=esphome::usp_spotify_auth::SpotifyAuthApi;
constexpr char SCOPE[]="user-read-playback-state";
constexpr char QUEUE[]=R"({"currently_playing":null,"queue":[{"type":"track","uri":"spotify:track:A","name":"Song","artists":[{"name":"Artist"}]},{"type":"track","uri":"spotify:track:A","name":"Song","artists":[{"name":"Artist"}]}]})";
class Api : public ProductionApi {
 public:
  void prepare() {this->stage4_coordinator_->session(this->generation(),SCOPE);}
  void queue_read() {this->poll_queue_(this->generation(),"fixture_access",SCOPE);}
  void devices_read() {this->poll_devices_(this->generation(),"fixture_access",SCOPE);}
  void hold(std::mutex &signal,std::condition_variable &changed,bool &held,bool &release) {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    std::unique_lock<std::mutex> notify(signal);held=true;changed.notify_all();
    changed.wait(notify,[&](){return release;});
  }
  bool scratch_empty() const {
    const auto &scratch=*this->stage4_workspace_;
    return !scratch.queue.valid&&!scratch.queue.count&&!scratch.devices.valid&&!scratch.devices.count;
  }
};
void queue_cases(Api &api) {
  http_fixture::response=QUEUE;api.queue_read();
  usp_spotify_queue::Queue queue;uint32_t updated=0;
  assert(api.copy_queue(&queue,&updated)&&queue.valid&&queue.count==2);
  assert(queue.items[0].uri==queue.items[1].uri&&api.scratch_empty());
  std::mutex signal;std::condition_variable changed;bool held=false,release=false;
  std::thread holder([&](){api.hold(signal,changed,held,release);});
  {std::unique_lock<std::mutex> lock(signal);
   assert(changed.wait_for(lock,std::chrono::seconds(2),[&](){return held;}));}
  api.queue_read(); // A failed try-lock must not throw from unlock().
  {std::lock_guard<std::mutex> lock(signal);release=true;}changed.notify_all();holder.join();
  assert(api.scratch_empty());
  assert(api.copy_queue(&queue,&updated)&&queue.valid&&queue.count==2);
  http_fixture::response=R"({"currently_playing":null,"queue":[{"type":"track","uri":"https://invalid","name":"Bad","artists":[]}]})";
  api.queue_read();
  assert(api.copy_queue(&queue,&updated)&&!queue.valid&&queue.count==0&&updated==0);
  assert(api.scratch_empty());
}
void devices_cases(Api &api) {
  http_fixture::response=R"({"devices":[{"id":"speaker","name":"Desk","type":"speaker","volume_percent":42,"is_active":true,"is_private_session":false,"is_restricted":false,"supports_volume":true}]})";
  api.devices_read();usp_spotify_devices::Catalog catalog;uint32_t revision=0,updated=0;
  assert(api.copy_devices(&catalog,&revision,&updated)&&catalog.valid&&catalog.count==1);
  assert(catalog.devices[0].active&&revision>0&&api.scratch_empty());
  http_fixture::response=R"({"devices":[{"id":"bad"}]})";api.devices_read();
  assert(api.copy_devices(&catalog,&revision,&updated)&&!catalog.valid&&catalog.count==0&&revision==0);
  assert(api.scratch_empty());
}
int main() {
  esphome::web_server_base::WebServerBase base;
  esphome::web_server_base::global_web_server_base=&base;
  http_fixture::reset();esphome::host_time=100;http_fixture::status=200;
  {
    Api api;api.set_application_client_id("client123");
    api.set_application_redirect_uri("https://return.test/cb");
    api.set_session_key_id(5);api.setup();api.prepare();
    queue_cases(api);devices_cases(api);
    for(const auto &request:http_fixture::requests) {
      assert(request.method==HTTP_METHOD_GET);
      assert(request.url.find("https://api.spotify.com/v1/me/player/")==0);
    }
    assert(http_fixture::requests.size()==5);
  }
  host_tasks::join_all();
  std::cout<<"SPOTIFY_STAGE4_READS_BINDING_NATIVE=PASS real_adapter PSRAM_scratch busy_mutex_no_unlock malformed_clears_public_state duplicate_queue_kept GET_only fixture_no_network\n";
}
