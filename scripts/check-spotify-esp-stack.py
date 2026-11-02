#!/usr/bin/env python3
"""Measure bounded Spotify stack frames with the real ESP32 compiler.

Run after ESPHome --only-generate. Creates a diagnostic object and report only.
No firmware image is linked and no device is contacted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware/.esphome/build/ukraine-status-panel"
LIMITS = {
    "Vault::store": 256,
    "Vault::load": 128,
    "Vault::clear": 128,
    "Engine::persistence_step_": 128,
    "SpotifyAuthApi::poll_stage4_reads_": 256,
    "SpotifyAuthApi::poll_devices_": 1024,
    "SpotifyAuthApi::poll_queue_": 1024,
    "SpotifyAuthApi::copy_queue": 128,
    "SpotifyAuthApi::copy_devices": 128,
}


def require_production_flags(row):
    """Reject stale debug flags before reporting production stack sizes."""
    args = shlex.split(row["command"])
    options = [value for value in args if value.startswith("-O")]
    assert options == ["-Os"], ("Wrong production optimization", options)
    config = Path(row["directory"]) / "config/sdkconfig.h"
    assert "#define CONFIG_COMPILER_OPTIMIZATION_SIZE 1" in config.read_text()


def compile_command(row, output):
    """Use fresh headers and keep target flags without altering objects."""
    require_production_flags(row)
    arguments = shlex.split(row["command"])
    # A cached PCH can contain the previous firmware version or declarations.
    for index in reversed(range(len(arguments) - 1)):
        if (
            arguments[index] == "-include"
            and "cmake_pch" in arguments[index + 1]
        ):
            end = index + 2
            del arguments[index:end]
    command = []
    skip = False
    for item in arguments:
        if skip:
            skip = False
        elif item in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif item not in (
            "-MD",
            "-MMD",
            "-MP",
            "-Winvalid-pch",
            "-fpch-preprocess",
        ):
            command.append(item)
    return command + ["-fstack-usage", "-o", str(output / "stack-target.o")]


def measure(output):
    """Bind generated source to canonical headers before target compilation."""
    component = ROOT / "components/usp_spotify_auth"
    generated = BUILD / "src/esphome/components/usp_spotify_auth"
    hashes = {}
    for name in (
        "session_vault.h",
        "session_vault_idf.h",
        "token_worker_engine.h",
        "spotify_auth.h",
        "spotify_auth.cpp",
        "queue_policy.h",
        "device_catalog.h",
        "stage4_state.h",
    ):
        data = (component / name).read_bytes()
        assert data == (generated / name).read_bytes(), name
        hashes[name] = hashlib.sha256(data).hexdigest()
    rows = json.loads((BUILD / "build/compile_commands.json").read_text())
    row = next(
        item
        for item in rows
        if item["file"].endswith("/usp_spotify_auth/spotify_auth.cpp")
    )
    with (output / "stack-compiler.log").open("w") as log:
        subprocess.run(
            compile_command(row, output),
            cwd=row["directory"],
            stdout=log,
            stderr=log,
            check=True,
            timeout=120,
        )
    return hashes


def check_frames(output):
    """Reject missing, dynamic or over-budget stack frames."""
    rows = (output / "stack-target.su").read_text().splitlines()
    measured = {}
    for symbol, limit in LIMITS.items():
        matches = [line for line in rows if symbol + "(" in line]
        assert len(matches) == 1, (symbol, matches)
        _, size, kind = matches[0].split("\t")
        assert kind == "static", (symbol, kind)
        measured[symbol] = int(size)
        assert int(size) <= limit, (symbol, size, limit)
    for line in rows:
        symbol, size, kind = line.split("\t")
        assert int(size) <= 3072, ("Oversized ESP frame", symbol, size)
        assert kind == "static", ("Unbounded ESP frame", symbol, kind)
    return measured


def main():
    """Save release evidence; hardware stability is checked separately."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    hashes = measure(output)
    frames = check_frames(output)
    all_frames = (output / "stack-target.su").read_text().splitlines()
    largest = max(int(row.split("\t")[1]) for row in all_frames)
    report = dict(
        status="PASS",
        actual_esp_target=True,
        frames=frames,
        limits=LIMITS,
        all_frames_limit_bytes=3072,
        frames_checked=len(all_frames),
        largest_frame_bytes=largest,
        source_sha256=hashes,
        image_built=False,
        device_contact=False,
    )
    (output / "stack-proof.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    logger.info("SPOTIFY_ESP_STACK_BUDGET=PASS {}", frames)


if __name__ == "__main__":
    main()
