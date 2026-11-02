#include "neptun_threat_parser.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using namespace esphome::usp_neptun;

template<class Scanner> Scanner parse(const std::string &text, size_t chunk) {
  Scanner scanner;
  for (size_t index = 0; index < text.size(); index += chunk)
    scanner.feed(text.data() + index, std::min(chunk, text.size() - index));
  return scanner;
}

void timestamps() {
  uint32_t epoch = 0;
  assert(parse_iso8601_utc("2026-10-20T20:23:48.521Z", &epoch));
  assert(epoch > 1700000000U);
  assert(parse_iso8601_utc("2024-02-29T00:00:00Z", &epoch));
  assert(!parse_iso8601_utc("2025-02-29T00:00:00Z", &epoch));
  assert(!parse_iso8601_utc("2026-10-20T20:23:48+00:00", &epoch));
}

std::string contract_json() {
  return R"({"serverTime":"2026-10-20T20:23:48.521Z","threats":[
 {"id":"good1","region":"Київ","type":"uav","status":"active","lat":50.45,"lon":30.52,"heading":123.4,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:48Z"},
 {"id":"good2","region":"Київ","type":"recon","status":"active","lat":50.55,"lon":30.62,"heading":222.0,"positionQuality":"approx","presumptiveCourse":true,"updatedAt":"2026-10-20T20:21:48Z"},
 {"id":"stale","region":"Київ","type":"uav","status":"active","lat":50.4,"lon":30.4,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:03:47Z"},
 {"id":"area","region":"Київ","type":"uav","status":"active","lat":50.4,"lon":30.4,"areaOnly":true,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"adv","region":"Київ","type":"uav","status":"active","lat":50.4,"lon":30.4,"advisory":true,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"nostatus","region":"Київ","type":"uav","lat":50.4,"lon":30.4,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"unknown","region":"Київ","type":"unknown","status":"active","lat":50.4,"lon":30.4,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"quality","region":"Київ","type":"uav","status":"active","lat":50.4,"lon":30.4,"positionQuality":"estimated","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"badlat","region":"Київ","type":"uav","status":"active","lat":null,"lon":30.4,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"},
 {"id":"outside","region":"Київ","type":"uav","status":"active","lat":10.0,"lon":10.0,"positionQuality":"confirmed","updatedAt":"2026-10-20T20:22:00Z"}
]})";
}

void filters() {
  const auto text = contract_json();
  for (size_t chunk : {1U, 5U, 37U, 4096U}) {
    const auto scanner = parse<ThreatScanner>(text, chunk);
    const auto &result = scanner.result();
    assert(scanner.complete());
    assert(result.entries == 10 && result.point_candidates == 3);
    assert(result.point_count == 2 && result.point_dropped_capacity == 0);
    assert(result.points[0].kind == ThreatPointKind::UAV);
    assert(result.points[0].quality == ThreatPointQuality::CONFIRMED);
    assert(result.points[0].heading_deg == 123);
    assert(result.points[1].kind == ThreatPointKind::RECON);
    assert(result.points[1].quality == ThreatPointQuality::APPROXIMATE);
    assert(result.points[1].heading_deg == -1);
  }
}

std::string capacity_json() {
  std::ostringstream out;
  out << R"({"threats":[)";
  for (int index = 0; index < 16; ++index) {
    if (index) out << ',';
    const int second = 59 - index;
    out << "{\"id\":\"p" << index
        << "\",\"region\":\"Київ\",\"type\":\"uav\",\"status\":\"active\",";
    out << "\"lat\":50.45,\"lon\":30.52,\"positionQuality\":\"confirmed\",";
    out << "\"updatedAt\":\"2026-10-20T20:22:";
    if (second < 10) out << '0';
    out << second << "Z\"}";
  }
  out << R"()],"serverTime":"2026-10-20T20:23:48Z"})";
  return out.str();
}

void capacity() {
  const auto scanner = parse<ThreatScanner>(capacity_json(), 11);
  const auto &result = scanner.result();
  assert(scanner.complete());
  assert(result.point_candidates == 16);
  assert(result.point_count == THREAT_POINT_CAPACITY);
  assert(result.point_dropped_capacity == 4);
  for (size_t index = 1; index < result.point_count; ++index)
    assert(result.points[index - 1].updated_epoch >= result.points[index].updated_epoch);
}

int main(int argc, char **argv) {
  timestamps();
  filters();
  capacity();
  if (argc == 2) {
    std::ifstream file(argv[1]);
    const std::string raw((std::istreambuf_iterator<char>(file)), {});
    const auto scanner = parse<ThreatScanner>(raw, 29);
    assert(scanner.complete());
    assert(scanner.result().server_epoch != 0);
    assert(scanner.result().point_count == THREAT_POINT_CAPACITY);
    std::printf("THREAT_POINT_LIVE_SCHEMA=PASS entries=%u candidates=%u points=%u\n",
                scanner.result().entries, scanner.result().point_candidates,
                scanner.result().point_count);
  }
  std::puts("NEPTUN_THREAT_POINTS=PASS ttl fragmented filters quality heading newest12");
}
