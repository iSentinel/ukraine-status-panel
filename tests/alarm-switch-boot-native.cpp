// Compile the installed framework's complete Switch and TemplateSwitch code.
// Only platform facilities (storage, callbacks, base entity and logs) are fake.
#include <cassert>
#include <cstdlib>
#include <iostream>
#include "esphome/components/template/switch/template_switch.h"

namespace {
bool stored_policy;
int changes;
esphome::optional<bool> policy() { return stored_policy; }

void verify_boot(esphome::switch_::SwitchRestoreMode mode, bool saved) {
  stored_policy = saved;
  changes = 0;
  esphome::preference_reads = 0;
  esphome::preference_writes = 0;
  esphome::template_::TemplateSwitch control;
  control.set_restore_mode(mode);
  control.set_state_lambda(policy);
  control.get_turn_on_trigger()->add_callback([] {
    stored_policy = true;
    ++changes;
  });
  control.get_turn_off_trigger()->add_callback([] {
    stored_policy = false;
    ++changes;
  });
  control.setup();
  assert(changes == 0 && "Boot must not execute a setting action");
  assert(stored_policy == saved && "Boot must preserve the owner's value");
  control.loop();
  assert(control.state == saved);
  control.control(!saved);
  control.loop();
  assert(changes == 1 && control.state == !saved);
  control.control(saved);
  control.loop();
  assert(changes == 2 && control.state == saved);
  assert(esphome::preference_reads == 0);
  assert(esphome::preference_writes == 0);
}

void verify_reboots(esphome::switch_::SwitchRestoreMode mode) {
  // Fresh installation uses the owning global's true default.
  verify_boot(mode, true);
  // Explicit ON and OFF must both survive subsequent setup calls.
  for (int i = 0; i < 1000; ++i) verify_boot(mode, (i % 2) == 0);
}
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const auto mode = static_cast<esphome::switch_::SwitchRestoreMode>(
      std::atoi(argv[1]));
  verify_reboots(mode);
  std::cout << "ALARM_SWITCH_BOOT_NATIVE=PASS freshON savedON savedOFF "
               "actionsONOFF reboots1000 switchStorageReads0Writes0\n";
}
