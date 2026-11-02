#pragma once
namespace esphome {
template<typename... Args> class Trigger {
 public:
  void trigger(Args...) { ++calls; }
  unsigned calls{0};
};
}
