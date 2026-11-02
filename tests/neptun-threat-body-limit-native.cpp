#include "neptun_threat_parser.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

using esphome::usp_neptun::ThreatScanner;

namespace {

std::string make_body(size_t count) {
  const std::string pad(150, 'x');
  std::string body = "{\"serverTime\":\"2026-10-22T11:00:00Z\",\"threats\":[";
  for (size_t index = 0; index < count; ++index) {
    if (index != 0) body += ',';
    body += "{\"region\":\"Київ\",\"type\":\"uav\",\"status\":\"active\",";
    body += "\"advisory\":false,\"extra\":\"" + pad + "\"}";
  }
  body += "]}";
  return body;
}

ThreatScanner parse(const std::string &body, size_t chunk) {
  ThreatScanner scanner;
  for (size_t pos = 0; pos < body.size(); pos += chunk) {
    scanner.feed(body.data() + pos, std::min(chunk, body.size() - pos));
  }
  return scanner;
}

}  // namespace

int main() {
  const std::string mass_attack = make_body(600);
  assert(mass_attack.size() > 131072U);
  assert(mass_attack.size() < 1048576U);
  for (size_t chunk : {97U, 2048U}) {
    const auto scanner = parse(mass_attack, chunk);
    assert(scanner.complete());
    assert(!scanner.result().overflow);
    assert(scanner.result().entries == 600U);
    assert(scanner.result().global.total == 600U);
  }

  const std::string excessive = make_body(4800);
  assert(excessive.size() > 1048576U);
  const auto overflow = parse(excessive, 2048U);
  assert(overflow.result().overflow);
  assert(!overflow.complete());

  std::printf(
      "NEPTUN_THREAT_BODY_LIMIT=PASS mass_bytes=%zu entries=600 "
      "cap_bytes=1048576 oversize_bytes=%zu fail_closed=1\n",
      mass_attack.size(), excessive.size());
  return 0;
}
