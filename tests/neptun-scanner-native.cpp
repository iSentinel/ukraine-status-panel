#include "neptun_alert_parser.h"
#include "neptun_threat_parser.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace esphome::usp_neptun;

template<class Scanner> Scanner parse(const std::string &text, size_t chunk = 1) {
  Scanner scanner;
  for (size_t index = 0; index < text.size(); index += chunk)
    scanner.feed(text.data() + index, std::min(chunk, text.size() - index));
  return scanner;
}

void threats() {
  const std::string text = R"({"threats":[
    {"region":"Київ","type":"ballistic","status":"active","advisory":false},
    {"region":"Київська область","type":"ballistic","advisory":true},
    {"region":"Київ","type":"missile","status":"stale"},
    {"region":"Київ","type":"kab","status":"resolved"},
    {"region":"Київ","type":"uav","status":"active","advisory":null},
    {"region":"Київ","type":"recon","status":"active","advisory":"false"},
    {"region":"Київ","type":"fpv","status":"active","nested":{"type":"ballistic"}},
    {"region":"","type":"uav","status":"active"}
  ]})";
  for (size_t chunk : {1, 7, 43, 1024}) {
    const auto scanner = parse<ThreatScanner>(text, chunk);
    assert(scanner.complete() && scanner.result().entries == 8);
    const auto &active = scanner.result().active_regions[region_alert_index(31)];
    assert(active.total == 2 && active.ballistic == 1 && active.fpv == 1);
    assert(scanner.result().active_regions[region_alert_index(14)].total == 0);
    assert(scanner.result().unknown_regions == 1);
  }
  for (size_t cut = 0; cut < text.size(); ++cut)
    assert(!parse<ThreatScanner>(text.substr(0, cut)).complete());
  assert(parse<ThreatScanner>(R"({"threats":[]})").complete());
  assert(!parse<ThreatScanner>(R"({"threats":[]} trailing)").complete());
  const std::string unknown = "{\"threats\":[{\"region\":\"Київ\",\"type\":\"uav\",\"extra\":\"" +
      std::string(300, 'x') + "\"}]}";
  const auto long_field = parse<ThreatScanner>(unknown);
  assert(long_field.complete() && long_field.result().active_regions[region_alert_index(31)].uav == 1);
}


/** Count active non-advisory threats while retaining input diagnostics. */
void active_summary_counts() {
  const std::string body = R"({"threats":[
    {"region":"Київ","type":"uav","status":"active","advisory":false},
    {"region":"Київ","type":"missile","status":"stale"},
    {"region":"Київ","type":"ballistic","status":"resolved"},
    {"region":"Київ","type":"kab","status":"active","advisory":true},
    {"region":"Київ","type":"fpv","status":"active","advisory":null},
    {"region":"Київ","type":"recon","status":null},
    {"region":"unknown-region","type":"ballistic","status":"active"},
    {"region":"Київ","type":"kab","status":"active","areaOnly":true},
    {"region":"Київ","type":"mig31k"},
    {"region":"Київ","type":"recon","status":false},
    {"region":"unknown-region","type":"missile","status":"resolved"}
  ]})";
  for (size_t chunk : {1, 2, 7, 43, 4096}) {
    const auto scanner = parse<ThreatScanner>(body, chunk);
    const auto &result = scanner.result();
    assert(scanner.complete() && result.entries == 11);
    assert(result.unknown_regions == 2);
    assert(result.global.total == 4 && result.global.uav == 1);
    assert(result.global.ballistic == 1 && result.global.kab == 1);
    assert(result.global.mig31k == 1 && result.global.missile == 0);
    assert(result.global.fpv == 0 && result.global.recon == 0);
    const auto &region = result.regions[region_alert_index(31)];
    const auto &active = result.active_regions[region_alert_index(31)];
    assert(region.total == 3 && region.uav == 1 && region.kab == 1);
    assert(region.mig31k == 1 && region.ballistic == 0);
    assert(region.total == active.total && region.uav == active.uav);
    assert(region.kab == active.kab && region.mig31k == active.mig31k);
    assert(result.point_count == 0);
  }
  std::puts("NEPTUN_ACTIVE_COUNTS=PASS active_only legacy area_only unknown_region");
}


void alerts_unicode_and_normalization() {
  const std::string escaped = R"({"raions":[
    {"name":"\u043c. \u041a\u0438\u0457\u0432","level":"red"}
  ],"oblasts":[]})";
  for (size_t chunk : {1, 2, 5, 64}) {
    const auto scanner = parse<AlertScanner>(escaped, chunk);
    assert(scanner.complete());
    assert(scanner.result().entries == 1);
    assert(scanner.result().unknown_regions == 0);
    assert(scanner.result().levels[region_alert_index(31)] == 2);
  }

  const std::string spaces = R"({"raions":[
    {"oblast":"  Київська   область  ","level":"yellow"}
  ],"oblasts":[]})";
  const auto scanner = parse<AlertScanner>(spaces, 3);
  assert(scanner.complete());
  assert(scanner.result().entries == 1);
  assert(scanner.result().unknown_regions == 0);
  assert(scanner.result().levels[region_alert_index(14)] == 1);
}


/** Decode values and keys once, independent of HTTP fragment boundaries. */
void unicode_fields() {
  const std::string body = R"({"ra\u0069ons":[
    {"\u006eame":"\u00a0м.\u202fКиїв\u00a0","level":"r\u0065d",
     "comment":"\uD83D\uDE80"}
  ],"oblasts":[]})";
  const std::string threat = R"({"thr\u0065ats":[
    {"region":"\u043c. \u041a\u0438\u0457\u0432",
     "type":"\u0075av","status":"\u0061ctive","advisory":false}
  ]})";
  for (size_t chunk : {1, 2, 3, 7, 64, 4096}) {
    const auto alerts = parse<AlertScanner>(body, chunk);
    assert(alerts.complete() && alerts.result().unknown_regions == 0);
    assert(alerts.result().levels[region_alert_index(31)] == 2);
    const auto threats = parse<ThreatScanner>(threat, chunk);
    assert(threats.complete() && threats.result().unknown_regions == 0);
    assert(threats.result().global.uav == 1);
    assert(threats.result().regions[region_alert_index(31)].uav == 1);
  }
  for (size_t cut = 0; cut < body.size(); ++cut)
    assert(!parse<AlertScanner>(body.substr(0, cut)).complete());
  const auto literal = parse<AlertScanner>(
      R"({"raions":[{"name":"\\u043c. Київ","level":"red"}],"oblasts":[]})");
  assert(literal.complete() && literal.result().unknown_regions == 1);
}

/** Never convert malformed escapes or truncated names into trusted regions. */
void invalid_unicode_fields() {
  for (const auto *suffix : {R"(\q)", R"(\u12G4)", R"(\uD800)",
                            R"(\uDC00)", R"(\uD800\u0041)",
                            R"(\u0000suffix)"}) {
    const std::string name = std::string("Київ") + suffix;
    const auto alerts = parse<AlertScanner>(
        "{\"raions\":[{\"name\":\"" + name +
        "\",\"level\":\"red\"}],\"oblasts\":[]}");
    assert(!alerts.complete());
    const auto threats = parse<ThreatScanner>(
        "{\"threats\":[{\"region\":\"" + name +
        "\",\"type\":\"uav\",\"status\":\"active\"}]}");
    assert(!threats.complete());
  }
  const std::string too_long = "Київ" + std::string(80, ' ') + "suffix";
  assert(!parse<AlertScanner>("{\"raions\":[{\"name\":\"" + too_long +
      "\",\"level\":\"red\"}],\"oblasts\":[]}").complete());
  assert(!parse<ThreatScanner>("{\"threats\":[{\"region\":\"" + too_long +
      "\",\"type\":\"uav\"}]}").complete());
  const std::string control = std::string("Київ") + char(1);
  assert(!parse<AlertScanner>("{\"raions\":[{\"name\":\"" + control +
      "\",\"level\":\"red\"}],\"oblasts\":[]}").complete());
  assert(region_id_from_neptun_oblast("Киiв") == 0);
}

/** Check four-byte UTF-8 writes and the reserved NUL byte at exact capacity. */
void decoder_capacity() {
  JsonStringDecoder decoder;
  JsonStringDecoder::Result result{JsonStringDecoder::MORE};
  for (const char value : std::string(R"(\uD83D\uDE80)"))
    result = decoder.consume(value);
  assert(result.kind == JsonStringDecoder::CODEPOINT && result.value == 0x1F680);
  struct Guard { char before{'B'}, bytes[4]{}, after{'A'}; } guard;
  size_t length = 0;
  assert(!append_json_character(result, guard.bytes, sizeof(guard.bytes), length));
  assert(length == 0 && guard.before == 'B' && guard.after == 'A');
  char enough[5]{};
  assert(append_json_character(result, enough, sizeof(enough), length));
  assert(length == 4 && std::string(enough, 4) == "\xF0\x9F\x9A\x80");
  assert(decoder.consume('"').kind == JsonStringDecoder::END);
}

void alerts() {
  const std::string text = R"({"version":1,"raions":[
    {"name":"Район","oblast":"Київська область","level":"red","reasons":["drone"]},
    {"name":"Київ","level":"yellow"}
  ],"oblasts":[]})";
  for (size_t chunk : {1, 13, 1000}) {
    const auto scanner = parse<AlertScanner>(text, chunk);
    assert(scanner.complete() && scanner.result().entries == 2);
    assert(scanner.result().levels[region_alert_index(14)] == 2);
    assert(scanner.result().levels[region_alert_index(31)] == 1);
  }
  for (size_t cut = 0; cut < text.size(); ++cut)
    assert(!parse<AlertScanner>(text.substr(0, cut)).complete());
  assert(parse<AlertScanner>(R"({"raions":[],"oblasts":[]})").complete());
  assert(!parse<AlertScanner>(R"({"raions":[]})").complete());
  assert(!parse<AlertScanner>(R"({"raions":"oblasts"})").complete());
}

int main(int argc, char **argv) {
  threats();
  active_summary_counts();
  alerts();
  alerts_unicode_and_normalization();
  unicode_fields(); invalid_unicode_fields(); decoder_capacity();
  std::puts("NEPTUN_JSON_UNICODE=PASS escaped_keys values surrogates fragmented "
            "spaces malformed_rejected NUL_rejected capacity");
  if (argc == 3) {
    std::ifstream first(argv[1]), second(argv[2]);
    const std::string raw_threats((std::istreambuf_iterator<char>(first)), {});
    const std::string raw_alerts((std::istreambuf_iterator<char>(second)), {});
    const auto t = parse<ThreatScanner>(raw_threats);
    const auto a = parse<AlertScanner>(raw_alerts);
    assert(t.complete() && a.complete());
    std::printf("LIVE_SCHEMA=PASS threats=%u alert_entries=%u unknown_regions=%u\n",
                t.result().entries, a.result().entries, a.result().unknown_regions);
  }
  std::puts("NEPTUN_SCANNERS=PASS fragmented truncated advisory stale resolved nested alias long_unknown_field");
}
