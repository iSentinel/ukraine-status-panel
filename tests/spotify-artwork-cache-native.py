#!/usr/bin/env python3
"""Check artwork coordination with actual decoding and explicit hardware substitutes."""
import argparse
from pathlib import Path
import runpy
import shutil
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def compile_decoder(module, directory, lvgl, flags):
    """Compile the verified pinned C source once for both coordination cases."""
    object_path = directory / "tjpgd.o"
    module["checked"](
        [
            shutil.which("gcc") or "gcc",
            "-std=c99",
            *flags,
            "-c",
            str(lvgl / "src/libs/tjpgd/tjpgd.c"),
            "-o",
            str(object_path),
        ]
    )
    return object_path


def cases(module, directory):
    """Compile real LVGL C once, then check state and threaded pixel ownership."""
    lvgl = module["lvgl_path"]()
    flags = module["common_flags"](lvgl) + [
        "-pthread",
        "-I",
        str(ROOT / "tests/spotify_artwork_host"),
    ]
    object_path = compile_decoder(module, directory, lvgl, flags)
    for name, cwd in (
        ("spotify-artwork-cache-native.cpp", ROOT),
        (
            "spotify-artwork-cache-integrated-native.cpp",
            ROOT / "tests/spotify_artwork_fixtures",
        ),
    ):
        binary = directory / Path(name).stem
        module["checked"](
            [
                shutil.which("g++") or "g++",
                "-std=c++17",
                *flags,
                str(ROOT / "components/usp_spotify_auth/artwork_decoder.cpp"),
                str(ROOT / "tests" / name),
                str(object_path),
                "-o",
                str(binary),
            ]
        )
        module["checked"]([str(binary)], cwd=cwd)


def main():
    """Use temporary builds and never contact providers or a physical display."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    module = runpy.run_path(
        str(ROOT / "tests/spotify-artwork-decoder-native.py")
    )
    with tempfile.TemporaryDirectory(prefix="usp-artwork-cache-") as name:
        cases(module, Path(name))
    logger.info("SPOTIFY_ARTWORK_CACHE_NATIVE=PASS provider_io=0 device_io=0")


if __name__ == "__main__":
    main()
