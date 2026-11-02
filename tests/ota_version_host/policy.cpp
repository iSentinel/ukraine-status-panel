#include "version_policy.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

/** Exercise the actual OtaGuard entry point used by manifest validation. */
bool allowed(const std::string &target, const std::string &current, bool override) {
  return OtaGuard{}.version_allowed(target, current, override);
}
using esphome::usp_ota_version::parse;
using esphome::usp_ota_version::Version;

/** Verify every pair in the standard prerelease ordering sequence. */
unsigned check_order() {
  std::vector<std::string> ordered = {
    "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
    "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
    "1.0.0-rc.1", "1.0.0"
  };
  unsigned cases = 0;
  for (size_t i = 0; i < ordered.size(); ++i) {
    for (size_t j = 0; j < ordered.size(); ++j) {
      assert(allowed(ordered[i], ordered[j], false) == (i >= j));
      assert(allowed(ordered[i], ordered[j], true));
      cases += 2;
    }
  }
  return cases;
}

/** Reject malformed targets and current versions, even with an override. */
unsigned check_malformed() {
  unsigned cases = 0;
  std::vector<std::string> malformed = {
    "", "1", "1.2", "1.2.3.4", "1.2.3-", "1.2.3+",
    "1.2.3-a..b", "1.2.3-a.", "1.2.3-.a", "1.2.3+..",
    "1.2.3+a+z", "1.2.3-a b", "1.2.3-a_b", " 1.2.3",
    "1.2.3\n", "01.2.3", "1.02.3", "1.2.03", "1.2.3-01",
    "1.2.3-a.01", "4294967296.0.0", "1.2.9999999999999999999",
    "1.2.3-\xc3\xa9", "v1.2.3", std::string("1.2.3-x\0y", 9)
  };
  for (const auto &value : malformed) {
    for (bool override : {false, true}) {
      assert(!allowed(value, "1.2.3", override));
      assert(!allowed("1.2.3", value, override));
      cases += 2;
    }
  }
  return cases;
}

/** Cover metadata, numeric limits and project development versions. */
void check_edges() {
  assert(allowed("1.2.3", "1.2.3-dev", false));
  assert(!allowed("1.2.3-dev", "1.2.3", false));
  assert(allowed("1.2.3+002", "1.2.3+001", false));
  assert(allowed("1.2.3-dev+002", "1.2.3-dev+001", false));
  assert(!allowed("1.2.3-dev.2", "1.2.3-dev.10", false));
  assert(!allowed("1.2.3-99999999999999999999999999999",
                  "1.2.3-100000000000000000000000000000", false));
  assert(allowed("2.0.0-dev", "1.99.99", false));
  assert(!allowed("1.99.99", "2.0.0-dev", false));
  assert(allowed("4294967295.4294967295.4294967295", "0.0.0", false));
  Version parsed;
  assert(parse("0.115.273-dev", parsed));
  assert(parse("1.2.3-0A+000", parsed));
  assert(parse("1.2.3--+a-b", parsed));
  assert(!parse("1.2.3-00", parsed));
  assert(!allowed("0.115.273-dev", "0.115.273", false));
}


/** Keep manifest core validation consistent with the full suffix parser. */
void check_core() {
  uint32_t core[3]{};
  assert(OtaGuard::parse_version_core_("0.115.273-dev+build", core));
  assert(core[0] == 0 && core[1] == 115 && core[2] == 273);
  assert(!OtaGuard::parse_version_core_("0.115.273-", core));
  assert(!OtaGuard::parse_version_core_("0.115.273", nullptr));
}

int main() {
  unsigned cases = check_order() + check_malformed();
  check_edges();
  check_core();
  std::cout << "OTA_VERSION_POLICY_NATIVE=PASS cases=" << cases + 18 << "\n";
}
