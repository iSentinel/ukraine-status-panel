#!/usr/bin/env python3
"""Exercise Spotify Stage4 policies and binding under ASAN/UBSAN."""
import json
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_spotify_auth"
TESTS = ROOT / "tests"
AJ = (
    ROOT
    / "firmware/.esphome/build/ukraine-status-panel/managed_components/bblanchon__arduinojson/src"
)
SHIMS = TESTS / "spotify_token_host/shims"


def compiler_prefix() -> list[str]:
    """Return common sanitizer and warning flags."""
    return [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O0",
        "-g",
        "-pthread",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(COMP),
        "-I",
        str(ROOT / "include"),
        "-isystem",
        str(AJ),
    ]


def compile_run(source: str, extra: list[str] | None = None) -> None:
    """Compile one native fixture and execute it once."""
    with tempfile.TemporaryDirectory(prefix="usp-stage4-") as temp:
        binary = Path(temp) / "case"
        command = compiler_prefix()
        if extra:
            command.extend(extra)
        command.extend([str(TESTS / source), "-o", str(binary)])
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=60)


def main() -> None:
    """Run every permanent Stage4 native contract."""
    assert (
        json.loads((AJ.parent / "library.json").read_text())["version"]
        == "7.4.3"
    )
    for source in (
        "spotify-stage4-device-policy-native.cpp",
        "spotify-stage4-model-clear-native.cpp",
        "spotify-stage4-value-policy-native.cpp",
        "spotify-stage4-mode-policy-native.cpp",
        "spotify-stage4-queue-policy-native.cpp",
        "spotify-stage4-coordinator-native.cpp",
        "spotify-stage4-playback-extended-native.cpp",
    ):
        compile_run(source)
    compile_run(
        "spotify-stage4-transport-native.cpp",
        [
            "-I",
            str(SHIMS),
            "-DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1",
            str(COMP / "stage4_transport.cpp"),
        ],
    )
    logger.info(
        "SPOTIFY_STAGE4_NATIVE=PASS policies binding transport provider_io=0 device_io=0"
    )


if __name__ == "__main__":
    main()
