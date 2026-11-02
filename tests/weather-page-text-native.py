#!/usr/bin/env python3
"""Check five snapshot-language Weather captions on copies, without a device."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile
import textwrap
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/fixtures/weather-page"


def fixture(rows):
    """Use exact previous callbacks and independent named-key golden strings."""
    text = '#include <cassert>\n#include <cstring>\n#include <string>\n#include "usp_weather_page_text.h"\n'
    text += "struct View { unsigned language; } weather_ui_view;\nunsigned ui_language;\n#define id(name) name\n"
    for index, row in enumerate(rows):
        text += (
            "std::string before"
            + str(index)
            + "() {\n"
            + row["before"]
            + "\n}\n"
        )
        text += (
            "std::string after"
            + str(index)
            + "() {\n"
            + row["after"]
            + "\n}\n"
        )
    text += "int main() {\nusing K=usp_weather_page_text::Key;\n"
    text += (
        "const K keys[]={" + ",".join("K::" + r["key"] for r in rows) + "};\n"
    )
    text += "const char *golden[3][5]={\n"
    for lang in range(3):
        text += (
            "{"
            + ",".join(
                json.dumps(r["values"][lang], ensure_ascii=False) for r in rows
            )
            + "},\n"
        )
    text += "};\nfor(unsigned l=0;l<256;++l) {\nweather_ui_view.language=l; ui_language=(l+1)%3;\n"
    text += "for(unsigned k=0;k<5;++k) assert(!std::strcmp(usp_weather_page_text::label(l,keys[k]),golden[l<3?l:2][k]));\n"
    text += 'for(unsigned bad:{5U,255U}) assert(!std::strcmp(usp_weather_page_text::label(l,static_cast<K>(bad)),""));\n'
    for index in range(5):
        text += (
            "assert(before" + str(index) + "()==after" + str(index) + "());\n"
        )
    return text + "}\n}\n"


def execute(base, name, header, rows, negative):
    """Compile every control before requiring execution to reject it."""
    directory = base / name
    directory.mkdir()
    (directory / "usp_weather_page_text.h").write_text(header)
    (directory / "test.cpp").write_text(fixture(rows))
    command = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O1",
        "-fsanitize=address,undefined",
        "-fno-pie",
        "-no-pie",
        str(directory / "test.cpp"),
        "-o",
        str(directory / "test"),
    ]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.log").write_text(built.stdout + built.stderr)
    built.check_returncode()
    result = subprocess.run(
        [str(directory / "test")], capture_output=True, text=True, timeout=30
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    assert (result.returncode != 0) == negative, (name, result.returncode)
    logger.info("{} compiled=0 run={}", name, result.returncode)
    return dict(name=name, compile_exit=0, run_exit=result.returncode)


def variants(header, rows):
    """Change copied captions, names, bounds, fallback or language source."""
    for name, before, after in [
        ("named_key", "WEATHER, NOW", "NOW, WEATHER"),
        ("caption", '"WEATHER"', '"WEATHER!"'),
        ("fallback", "language : 2", "language : 0"),
        ("invalid_bound", "index < 5", "index < 6"),
    ]:
        assert header.count(before) == 1, name
        yield name, header.replace(before, after, 1), rows
    for name, before, after in [
        ("consumer_key", "Key::WEATHER", "Key::NOW"),
        (
            "consumer_language",
            "id(weather_ui_view).language",
            "id(ui_language)",
        ),
    ]:
        copied = json.loads(json.dumps(rows))
        assert copied[0]["after"].count(before) == 1
        copied[0]["after"] = copied[0]["after"].replace(before, after, 1)
        yield name, header, copied


def run_all(base):
    """Compare actual current consumers and copied negative controls."""
    base.mkdir()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    source = ROOT / "firmware/ukraine-status-panel.yaml"
    before = source.read_bytes()
    header = (ROOT / "include/usp_weather_page_text.h").read_text()
    rows = current_rows()
    execute(base, "positive", header, rows, False)
    results = [
        execute(base, name, h, r, True)
        for name, h, r in variants(header, rows)
    ]
    assert source.read_bytes() == before
    report = dict(
        status="PASS",
        keys=5,
        values=15,
        consumers=5,
        named_key_comparisons=1280,
        callback_comparisons=1280,
        invalid_key_checks=512,
        compiled_negative_controls=results,
        snapshot_language_source_preserved=True,
        source_yaml_sha256=hashlib.sha256(before).hexdigest(),
        production_modified=False,
        device_contact=False,
    )
    with (base / "report.json").open("x") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info(
        "WEATHER_PAGE_NATIVE=PASS callbacks1280 negative{}", len(results)
    )


def current_rows():
    """Bind the fixed previous callbacks to actual current Weather widget lambdas."""
    rows = json.loads((OUT / "mapping.json").read_text())
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "include/usp_weather_page_text.h" in source
    marker = "  - id: refresh_weather_ui\n"
    assert source.count(marker) == 1
    script = source.split(marker, 1)[1].split("\n  - id:", 1)[0]
    for row in rows:
        marker = (
            "          id: " + row["widget"] + "\n          text: !lambda |-\n"
        )
        assert script.count(marker) == 1, row["widget"]
        tail = script.split(marker, 1)[1]
        lines = []
        for line in tail.splitlines():
            if line.strip() and not line.startswith("            "):
                break
            lines.append(line)
        body = textwrap.dedent("\n".join(lines)).strip()
        assert body.count("usp_weather_page_text::label(") == 1
        assert "id(weather_ui_view).language" in body
        row["after"] = body
    return rows


def main():
    """Run against the current YAML, retaining artifacts only when requested."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    if args.build_dir:
        base = args.build_dir.resolve()
        assert base.is_relative_to(ROOT / "artifacts")
        run_all(base)
    else:
        with tempfile.TemporaryDirectory(
            prefix="weather-page-", dir=ROOT / "artifacts"
        ) as name:
            run_all(Path(name) / "cases")


if __name__ == "__main__":
    main()
