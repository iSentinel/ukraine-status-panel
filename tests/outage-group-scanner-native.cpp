#include "../components/usp_outage/outage_group_scanner.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

using esphome::usp_outage::GroupObjectScanner;

static std::string extract(const std::string &json, size_t chunk,
                           const char *const *path, size_t path_len) {
  std::array<char, 4096> output{};
  GroupObjectScanner scanner{path, path_len, output.data(), output.size()};
  for (size_t offset = 0; offset < json.size(); offset += chunk) {
    const size_t end = std::min(json.size(), offset + chunk);
    for (size_t i = offset; i < end; ++i) {
      assert(scanner.consume(json[i]));
    }
  }
  assert(scanner.found());
  assert(!scanner.overflow());
  return std::string(output.data(), scanner.length());
}

int main() {
  const std::string probable = R"({
    "metadata":{"10.1":{"slots":{"0":[{"start":1,"end":2}]}}},
    "25":{"dsos":{"999":{"groups":{"10.1":{"slots":{"0":[]}}}},
    "902":{"groups":{"10.1":{"name":"10.1","slots":{"0":[
      {"start":480,"end":540,"type":"Definite"}]}}}}}}}
  })";
  const char *probable_path[]{"25", "dsos", "902", "groups", "10.1"};
  for (size_t chunk : {1U, 2U, 7U, 64U}) {
    const std::string selected = extract(
        probable, chunk, probable_path, std::size(probable_path));
    assert(selected.find("\"start\":480") != std::string::npos);
    assert(selected.find("\"start\":1") == std::string::npos);
  }
  const std::string planned = R"({
    "metadata":{"10.1":{"today":{"status":"wrong"}}},
    "10.1":{"today":{"status":"ScheduleApplies"}}
  })";
  const char *planned_path[]{"10.1"};
  const std::string planned_selected = extract(
      planned, 3, planned_path, std::size(planned_path));
  assert(planned_selected.find("ScheduleApplies") != std::string::npos);
  assert(planned_selected.find("wrong") == std::string::npos);
  std::puts("OUTAGE_GROUP_SCANNER=PASS exact planned/probable paths");
}
