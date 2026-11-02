#!/usr/bin/env python3
"""Run canonical Spotify worker cases with SDK substitutes and memory checks."""
import argparse
import json
import runpy
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SHIMS = ROOT / "tests/spotify_token_host/shims"
COMP = ROOT / "components/usp_spotify_auth"
ARDUINOJSON_VERSION = "7.4.3"


def dependency_path():
    """Require the same ArduinoJson version as the production build."""
    path = ROOT / "firmware/.esphome/build/ukraine-status-panel"
    path /= "managed_components/bblanchon__arduinojson/src"
    manifest = path.parent / "library.json"
    if not (path / "ArduinoJson.h").is_file() or not manifest.is_file():
        raise RuntimeError("Pinned ArduinoJson dependency is unavailable")
    if json.loads(manifest.read_text())["version"] != ARDUINOJSON_VERSION:
        raise RuntimeError("Native harness and production ArduinoJson differ")
    return path


def checked_run(command, timeout):
    """Preserve assertion and sanitizer details rather than hiding stderr."""
    result = subprocess.run(
        command, cwd=ROOT, capture_output=True, text=True, timeout=timeout
    )
    if result.returncode:
        logger.error("Native case failed: {}", result.stderr[-4000:])
        result.check_returncode()
    print(result.stdout, end="")


def lvgl_dependency():
    """Use the same pinned decoder verification as the dedicated JPEG checks."""
    module = runpy.run_path(
        str(ROOT / "tests/spotify-artwork-decoder-native.py")
    )
    return module["lvgl_path"]()


def native_includes():
    """Keep production headers and SDK substitutes explicit."""
    return [
        "-I",
        str(ROOT / "include"),
        "-I",
        str(COMP),
        "-I",
        str(SHIMS),
        "-I",
        str(lvgl_dependency()),
        "-isystem",
        str(dependency_path()),
    ]


def compiler_flags():
    """Enable address checks and the pinned real decoder in host builds."""
    return [
        shutil.which("g++") or "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-DLV_CONF_SKIP",
        "-DLV_USE_TJPGD=1",
    ] + native_includes()


def decoder_object(folder):
    """Compile actual LVGL C once for the auth/task translation units."""
    target = folder / "tjpgd.o"
    if not target.exists():
        lvgl = lvgl_dependency()
        command = [
            shutil.which("gcc") or "gcc",
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-DLV_CONF_SKIP",
            "-DLV_USE_TJPGD=1",
            "-I",
            str(lvgl),
            "-c",
            str(lvgl / "src/libs/tjpgd/tjpgd.c"),
            "-o",
            str(target),
        ]
        checked_run(command, 60)
    return target


def execute(folder, label, sources, definitions, repeat):
    """Compile once, then exercise the requested controlled interleavings."""
    binary = folder / label
    command = compiler_flags() + ["-D" + value for value in definitions]
    command += [str(path) for path in sources]
    if label in (
        "adapter",
        "adapter-no-auth",
        "binding",
        "artwork-binding",
        "controls-binding",
        "stage4-reads-binding",
    ):
        command += [str(decoder_object(folder))]
    command += ["-o", str(binary)]
    checked_run(command, 60)
    for attempt in range(repeat):
        logger.debug("Running {} attempt {}/{}", label, attempt + 1, repeat)
        checked_run([str(binary)], 30)


def core_cases():
    """Return pure token policy cases."""
    return [
        ("core", [ROOT / "tests/spotify-token-core-native.cpp"], []),
        ("engine", [ROOT / "tests/spotify-token-engine-native.cpp"], []),
    ]


def transport_cases():
    """Keep verified-TLS and missing-certificate behavior separate."""
    sources = [
        COMP / "token_transport.cpp",
        ROOT / "tests/spotify-token-transport-native.cpp",
    ]
    return [
        ("transport", sources, ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1"]),
        (
            "no-certificate",
            sources,
            ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=0", "HOST_EXPECT_NO_BUNDLE"],
        ),
    ]


def adapter_cases():
    """Use the actual task, adapter and transport translation units."""
    sources = [
        COMP / "spotify_auth.cpp",
        COMP / "control_transport.cpp",
        COMP / "stage4_transport.cpp",
        COMP / "token_transport.cpp",
        COMP / "playback_transport.cpp",
        COMP / "artwork_transport.cpp",
        COMP / "artwork_decoder.cpp",
    ]
    cert = ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1"]
    adapter = [ROOT / "tests/spotify-token-adapter-worker-native.cpp"]
    binding = [ROOT / "tests/spotify-token-binding-native.cpp"]
    return [
        ("adapter", sources + adapter, cert + ["USE_WEBSERVER_AUTH"]),
        ("adapter-no-auth", sources + adapter, cert),
        ("binding", sources + binding, cert + ["USE_WEBSERVER_AUTH"]),
        (
            "artwork-binding",
            sources + [ROOT / "tests/spotify-artwork-http-binding-native.cpp"],
            cert + ["USE_WEBSERVER_AUTH"],
        ),
    ]


def binding_cases():
    """Exercise explicit taps through the actual single-worker task."""
    sources = [
        COMP / name
        for name in (
            "spotify_auth.cpp",
            "control_transport.cpp",
            "stage4_transport.cpp",
            "token_transport.cpp",
            "playback_transport.cpp",
            "artwork_transport.cpp",
            "artwork_decoder.cpp",
        )
    ]
    return [
        (
            "controls-binding",
            sources + [ROOT / "tests/spotify-controls-binding-native.cpp"],
            ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1", "USE_WEBSERVER_AUTH"],
        ),
        (
            "stage4-reads-binding",
            sources + [ROOT / "tests/spotify-stage4-reads-binding-native.cpp"],
            ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1", "USE_WEBSERVER_AUTH"],
        ),
    ]


def cases():
    """Return all ten contracts in their original order."""
    return core_cases() + transport_cases() + adapter_cases() + binding_cases()


def main():
    """Allow a focused repeat run without rebuilding a case for each attempt."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=[case[0] for case in cases()])
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 100:
        parser.error("--repeat must be between 1 and 100")
    selected = [c for c in cases() if args.case is None or c[0] == args.case]
    with tempfile.TemporaryDirectory(prefix="usp-spotify-token-") as name:
        for label, sources, definitions in selected:
            execute(Path(name), label, sources, definitions, args.repeat)
    logger.info(
        "SPOTIFY_TOKEN_WORKER_NATIVE=PASS cases={} repeats={} provider_io=0 device_io=0",
        len(selected),
        args.repeat,
    )


if __name__ == "__main__":
    main()
