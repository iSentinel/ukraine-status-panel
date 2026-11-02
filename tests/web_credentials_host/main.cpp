#include "web_server_base.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <string>
#include <thread>
using esphome::web_server_base::WebServerBase;
using esphome::web_server_base::internal::OwnedCredentials;
struct Target : AsyncWebHandler {
  unsigned calls{0};
  void handleRequest(AsyncWebServerRequest *) override { ++calls; }
};
WebServerBase server;
Target target;

void check_owned_lifetime() {
  OwnedCredentials credentials;
  assert(!credentials.is_set());
  assert(credentials.snapshot().username_value() == nullptr);
  credentials.set_username("");
  assert(credentials.is_set());
  credentials.set_password(nullptr);
  assert(credentials.snapshot().password_value() == nullptr);
  std::string owner(32, 'a');
  server.set_auth_username("admin");
  server.set_auth_password(owner.c_str());
  server.init();
  server.add_handler(&target);
  owner.assign(4096, 'x');
  AsyncWebServerRequest request;
  request.verifier = [](const char *user, const char *password) {
    assert(std::string(user) == "admin");
    assert(std::string(password) == std::string(32, 'a'));
    return true;
  };
  last_handler->handleRequest(&request);
  assert(target.calls == 1);
}

void check_request_snapshot() {
  std::promise<void> entered, resume;
  auto entered_future = entered.get_future();
  auto resumed = resume.get_future();
  AsyncWebServerRequest request;
  request.verifier = [&](const char *user, const char *password) {
    const std::string before(password);
    assert(std::string(user) == "admin");
    entered.set_value();
    resumed.wait();
    assert(before == password);
    return true;
  };
  std::thread reader([&]() { last_handler->handleRequest(&request); });
  entered_future.wait();
  auto writer = std::async(std::launch::async, []() {
    server.set_auth_password(std::string(64, 'b').c_str());
  });
  const bool updated = writer.wait_for(std::chrono::milliseconds(500)) ==
                       std::future_status::ready;
  resume.set_value();
  reader.join();
  writer.get();
  assert(updated);  // Authentication must not retain the credentials mutex.
}

void check_parallel_updates() {
  std::atomic<bool> start{false};
  AsyncWebServerRequest request;
  request.verifier = [](const char *user, const char *password) {
    assert(std::string(user) == "admin");
    const std::string value(password);
    assert(value == std::string(32, 'a') || value == std::string(64, 'b'));
    return true;
  };
  std::thread writer([&]() {
    while (!start.load()) std::this_thread::yield();
    for (unsigned i = 0; i < 10000; ++i)
      server.set_auth_password(i % 2 ? std::string(32, 'a').c_str() :
                                      std::string(64, 'b').c_str());
  });
  start.store(true);
  for (unsigned i = 0; i < 10000; ++i) last_handler->handleRequest(&request);
  writer.join();
}

void check_rejected_request() {
  AsyncWebServerRequest request;
  request.verifier = [](const char *, const char *) { return false; };
  const unsigned before = target.calls;
  last_handler->handleRequest(&request);
  last_handler->handleBody(&request, nullptr, 0, 0, 0);
  last_handler->handleUpload(&request, "", 0, nullptr, 0, true);
  // Body/upload/final callbacks belong to one rejected HTTP request.
  assert(request.challenges == 1 && target.calls == before);
}

int main() {
  check_owned_lifetime();
  check_request_snapshot();
  check_parallel_updates();
  check_rejected_request();
}
