#include "secret_text.h"
#include "legacy_text_saver.h"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace esphome;
class Field : public usp_secret_text::SecretText {
 public:
  using SecretText::control;
  using SecretText::preference_key_;
};

/** Observe the public state boundary used by API and Web serializers. */
void require_empty(const Field &field) {
  assert(field.state.empty() && !field.published.empty());
  assert(std::all_of(field.published.begin(), field.published.end(),
                    [](const auto &value) { return value.empty(); }));
}

/** Restore bytes produced by the actual previous TemplateTextSaver<64>. */
void check_legacy_restore() {
  memory = Memory{};
  Field field;
  template_::LegacyText legacy;
  legacy.setup();
  assert(legacy.pref_->save("synthetic-legacy-key"));
  legacy.setup();
  assert(legacy.state == "synthetic-legacy-key");
  field.set_restore_value(true);
  field.setup();
  assert(field.secret_value() == "synthetic-legacy-key");
  assert(memory.save_calls == 1);
  require_empty(field);
}

/** Verify writes, internal trigger delivery, zero fill and persistence. */
void check_write_and_clear() {
  Field field;
  field.set_restore_value(true);
  field.setup();
  field.control("fixture-new");
  assert(field.secret_value() == "fixture-new");
  assert(field.get_set_trigger()->values.back() == "fixture-new");
  auto bytes = memory.records[field.preference_key_()];
  assert(bytes[0] == 11);
  assert(std::all_of(bytes.begin() + 12, bytes.end(),
                    [](uint8_t value) { return value == 0; }));
  Field restored;
  restored.set_restore_value(true);
  restored.setup();
  assert(restored.secret_value() == "fixture-new");
  require_empty(restored);
  field.clear_value();
  restored.setup();
  assert(restored.secret_value().empty());
  require_empty(field);
  require_empty(restored);
}

/** A staged WiFi password must not write preferences or survive a restart. */
void check_transient() {
  memory = Memory{};
  Field staged;
  staged.setup();
  staged.control("fixture-wifi");
  assert(staged.secret_value() == "fixture-wifi");
  assert(memory.save_calls == 0);
  Field rebooted;
  rebooted.setup();
  assert(rebooted.secret_value().empty());
  staged.clear_value();
  assert(staged.secret_value().empty() && memory.records.empty());
  require_empty(staged);
  require_empty(rebooted);
}

/** Reject direct overlength input and support the existing 64-byte limit. */
void check_lengths() {
  Field field;
  field.setup();
  field.control(std::string(64, 's'));
  assert(field.secret_value().size() == 64);
  unsigned triggers = field.get_set_trigger()->values.size();
  field.control(std::string(65, 'x'));
  assert(field.secret_value().size() == 64);
  assert(field.get_set_trigger()->values.size() == triggers);
  require_empty(field);
}

/** Keep failed writes retryable and preserve the previous stored bytes. */
void check_save_failure() {
  memory = Memory{};
  Field field;
  field.set_restore_value(true);
  field.setup();
  field.control("old-fixture");
  auto before = memory.records;
  memory.save_success = false;
  field.control("new-fixture");
  assert(field.warning && memory.records == before);
  assert(field.secret_value() == "new-fixture");
  memory.save_success = true;
  field.control("new-fixture");
  assert(!field.warning && memory.records != before);
  Field restored;
  restored.set_restore_value(true);
  restored.setup();
  assert(restored.secret_value() == "new-fixture");
  require_empty(field);
  require_empty(restored);
}

/** Reject corrupt restored lengths without publishing stored bytes. */
void check_corrupt() {
  memory = Memory{};
  Field field;
  memory.records[field.preference_key_()] = std::vector<uint8_t>(65, 255);
  field.set_restore_value(true);
  field.setup();
  assert(field.secret_value().empty());
  require_empty(field);
}

int main() {
  check_legacy_restore();
  check_write_and_clear();
  check_transient();
  check_lengths();
  check_save_failure();
  check_corrupt();
  std::cout << "SECRET_TEXT_NATIVE=PASS legacy/current/transient/clear/limits/retry/corrupt public_state_empty\n";
}
