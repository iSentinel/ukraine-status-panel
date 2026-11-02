#!/usr/bin/env python3
"""Check bounded JPEG decoding and external-RAM buffers without a device."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_spotify_auth"
FIXTURES = ROOT / "tests/spotify_artwork_fixtures"
SHIMS = ROOT / "tests/spotify_artwork_host"
PINNED = {
    "tjpgd.c": "869f12918ddd795c97ac36c281683fc3f18c81b9e0279c6219f8bbe70c03a26a",
    "tjpgd.h": "7f768047e0b1947cd1eb55e29890137645042bfdb03fdcb56634ad7df8224d66",
    "tjpgdcnf.h": "ca1d5bc5fd41f3800238c56ea4322d5c0824df4e5fce5eb47ef9989cdf00728e",
}


def checked(command, cwd=ROOT):
    """Preserve useful compiler and sanitizer diagnostics on failure."""
    result = subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, timeout=60
    )
    if result.returncode:
        logger.error("{}", result.stderr[-6000:])
        result.check_returncode()
    print(result.stdout, end="")


def lvgl_path():
    """Require the actual installed decoder; never substitute a copied version."""
    default = ROOT / "firmware/.esphome/build/ukraine-status-panel"
    default /= "managed_components/lvgl__lvgl"
    path = Path(os.environ.get("USP_LVGL_ROOT", default))
    for name, expected in PINNED.items():
        source = path / "src/libs/tjpgd" / name
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError("Pinned LVGL decoder differs: " + name)
    return path


def common_flags(lvgl):
    """Use memory checks and the explicitly enabled pinned decoder configuration."""
    return [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-DLV_CONF_SKIP",
        "-DLV_USE_TJPGD=1",
        "-I",
        str(lvgl),
        "-I",
        str(COMP),
    ]


def decoder_case(folder, compiler, c_compiler):
    """Compile actual LVGL C and canonical wrapper as separate translation units."""
    lvgl = lvgl_path()
    flags = common_flags(lvgl)
    object_path = folder / "tjpgd.o"
    checked(
        [
            c_compiler,
            "-std=c99",
            *flags,
            "-c",
            str(lvgl / "src/libs/tjpgd/tjpgd.c"),
            "-o",
            str(object_path),
        ]
    )
    binary = folder / "decoder"
    checked(
        [
            compiler,
            "-std=c++17",
            *flags,
            str(COMP / "artwork_decoder.cpp"),
            str(ROOT / "tests/spotify-artwork-decoder-native.cpp"),
            str(object_path),
            "-o",
            str(binary),
        ]
    )
    checked([str(binary)], cwd=FIXTURES)


def buffers_case(folder, compiler):
    """Exercise allocation failure, wrong RAM region and stable disjoint buffers."""
    binary = folder / "buffers"
    checked(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(COMP),
            "-I",
            str(SHIMS),
            str(ROOT / "tests/spotify-artwork-buffers-native.cpp"),
            "-o",
            str(binary),
        ]
    )
    checked([str(binary)])


def main():
    """Run deterministic synthetic-image cases with no network or hardware access."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--c-compiler", default="gcc")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    c_compiler = shutil.which(args.c_compiler)
    if not compiler or not c_compiler:
        raise FileNotFoundError("Both C and C++ compilers are required")
    with tempfile.TemporaryDirectory(prefix="usp-artwork-decoder-") as name:
        folder = Path(name)
        decoder_case(folder, compiler, c_compiler)
        buffers_case(folder, compiler)
    logger.info(
        "SPOTIFY_ARTWORK_DECODER_NATIVE=PASS provider_io=0 device_io=0"
    )


if __name__ == "__main__":
    main()
