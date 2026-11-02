#pragma once
#include "helpers.h"
namespace esphome {
template<typename... A> class Trigger {
 public:
  template<typename F> void add_callback(F &&f) { callbacks_.add(f); }
  void trigger(A... args) { callbacks_.call(args...); }
  void stop_action() {}
 private:
  LazyCallbackManager<void(A...)> callbacks_;
};
}
