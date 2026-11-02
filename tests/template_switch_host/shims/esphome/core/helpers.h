#pragma once
#include <functional>
#include <utility>
#include <vector>
#include "optional.h"
namespace esphome {
template<typename T> class Deduplicator {
 public:
  bool next(T value) {
    if (has_ && last_ == value) return false;
    has_ = true;
    last_ = value;
    return true;
  }
 private:
  bool has_{false};
  T last_{};
};
template<typename T> class LazyCallbackManager;
template<typename... A> class LazyCallbackManager<void(A...)> {
 public:
  template<typename F> void add(F &&f) { callbacks_.push_back(f); }
  void call(A... args) { for (auto &f : callbacks_) f(args...); }
 private:
  std::vector<std::function<void(A...)>> callbacks_;
};
}
