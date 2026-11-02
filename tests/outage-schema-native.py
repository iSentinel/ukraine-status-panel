#!/usr/bin/env python3
"""Run the production Yasno day parser against strict schema cases."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
ARDUINOJSON_VERSION = "7.4.3"
METHODS = ("parse_day_", "parse_slot_type_", "parse_day_status_")

FIXTURE = r'''
#include <ArduinoJson.h>
#include "outage_calendar.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
namespace esphome::usp_outage {
std::string safe_text(JsonVariantConst value) {
  const char *text = value | "";
  return text == nullptr ? std::string{} : std::string{text};
}
class OutageClient {
 public:
  static constexpr size_t MAX_DAY_SLOTS = 8;
  enum class SlotType : uint8_t { UNKNOWN=0, NOT_PLANNED=1, DEFINITE=2 };
  enum class DayStatus : uint8_t {
    UNKNOWN=0, WAITING=1, SCHEDULE_APPLIES=2, EMERGENCY=3, NO_OUTAGES=4
  };
  struct OutageSlot {
    uint16_t start_min{0};
    uint16_t end_min{0};
    SlotType type{SlotType::UNKNOWN};
  };
  struct DaySchedule {
    std::string date;
    DayStatus status{DayStatus::UNKNOWN};
    size_t count{0};
    std::array<OutageSlot, MAX_DAY_SLOTS> slots{};
  };
  bool parse_day_(JsonObjectConst object, DaySchedule *schedule) const;
  static SlotType parse_slot_type_(const std::string &value);
  static DayStatus parse_day_status_(const std::string &value);
};
'''

CASES = r'''
}
using namespace esphome::usp_outage;
bool decode(OutageClient &client, const char *payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  OutageClient::DaySchedule day;
  return client.parse_day_(doc.as<JsonObjectConst>(), &day);
}
int main() {
  OutageClient client;
  assert(decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"ScheduleApplies",
    "slots":[{"start":660,"end":870,"type":"Definite"}]
  })"));
  assert(decode(client, R"({
    "date":"2026-10-23T00:00:00+03:00",
    "status":"WaitingForSchedule","slots":[]
  })"));
  assert(decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"NoOutages","slots":[]
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"SchemaChanged","slots":[]
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00","slots":[]
  })"));
  assert(!decode(client, R"({
    "date":"","status":"ScheduleApplies","slots":[]
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"ScheduleApplies"
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"ScheduleApplies","slots":{}
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"ScheduleApplies",
    "slots":[{"start":60,"end":120,"type":"NewType"}]
  })"));
  assert(!decode(client, R"({
    "date":"2026-10-22T00:00:00+03:00",
    "status":"ScheduleApplies",
    "slots":[{"start":120,"end":60,"type":"Definite"}]
  })"));
  std::cout
      << "OUTAGE_SCHEMA_NATIVE=PASS strict_status slots date fail_closed\n";
}
'''



def dependency_path():
    """Return the exact ArduinoJson dependency used by production."""
    default = ROOT / "firmware/.esphome/build/ukraine-status-panel"
    default /= "managed_components/bblanchon__arduinojson/src"
    path = Path(os.environ.get("USP_ARDUINOJSON_INCLUDE", default))
    manifest = path.parent / "library.json"
    if not (path / "ArduinoJson.h").is_file() or not manifest.is_file():
        raise RuntimeError(
            "Pinned production ArduinoJson dependency is missing"
        )
    if json.loads(manifest.read_text())["version"] != ARDUINOJSON_VERSION:
        raise RuntimeError("Native test and production ArduinoJson differ")
    return path


def method(source, name):
    """Extract one complete production method by brace boundaries."""
    marker = "OutageClient::" + name + "("
    position = source.index(marker)
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def native_source():
    """Build the fixture from production parser methods, not a model."""
    source = (ROOT / "components/usp_outage/outage_client.cpp").read_text()
    return FIXTURE + "".join(method(source, name) for name in METHODS) + CASES


def main():
    """Compile with sanitizers and execute strict schema regression cases."""
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("Install g++ for the native outage schema gate")
    with tempfile.TemporaryDirectory(prefix="usp-outage-schema-") as directory:
        work = Path(directory)
        source = work / "test.cpp"
        binary = work / "test"
        source.write_text(native_source())
        command = [
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-O0", "-g", "-I", str(ROOT / "components/usp_outage"),
            "-isystem", str(dependency_path()),
            str(source), "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    logger.info("Yasno strict schema native parser checks passed")


if __name__ == "__main__":
    main()
