#!/usr/bin/env python3
"""Compare the actual native cache renderer against the existing map painter."""

import array
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def reference():
    """Load the independent mask-and-outline painter already used by QA."""
    path = ROOT / "tests/fullscreen-tiled-outline-equivalence.py"
    spec = importlib.util.spec_from_file_location("reference_map", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cases(module):
    """Cover all palettes, threat colors, mixed data and individual changes."""
    count = len(module.VIEW["regions"])
    for palette in module.PALETTES:
        for state in module.STATES:
            yield palette, [state] * count
        yield palette, [
            module.STATES[i % len(module.STATES)] for i in range(count)
        ]
        for index in range(count):
            states = [1] * count
            states[index] = 4
            yield palette, states


def native_colors(module, palette, states):
    """Use the same public colors; keep geometry out of this calculation."""
    values = [module.BACKGROUND]
    values.extend(module.PALETTES[palette][state] for state in states)
    values.append(module.OUTLINE)
    return array.array(
        "H",
        [
            ((rgb >> 8) & 0xF800)
            | ((rgb >> 5) & 0x07E0)
            | ((rgb >> 3) & 0x001F)
            for rgb in values
        ],
    ).tobytes()


def check_frames(module, matrix, native):
    """Require byte-for-byte equality of every final RGB565 pixel."""
    frame_bytes = module.VIEW["width"] * module.VIEW["height"] * 2
    if len(native) != len(matrix) * frame_bytes:
        raise RuntimeError("Native renderer returned an incomplete frame")
    for index, (palette, states) in enumerate(matrix):
        image = module.legacy(states, palette)
        expected = array.array(
            "H",
            [
                ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                for red, green, blue in image.getdata()
            ],
        ).tobytes()
        if expected != native[index * frame_bytes : (index + 1) * frame_bytes]:
            raise RuntimeError(
                f"Pixel mismatch at case {index}, palette {palette}"
            )
    logger.info(
        "FULLSCREEN_CACHE_EQUIVALENCE=PASS cases={} changed_pixels=0",
        len(matrix),
    )


def main():
    """Compile the firmware's pure renderer and compare its output frames."""
    if sys.byteorder != "little":
        raise RuntimeError(
            "This fixture currently requires a little-endian host"
        )
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("Install g++ to run the native map gate")
    module = reference()
    matrix = list(cases(module))
    with tempfile.TemporaryDirectory(prefix="usp-map-cache-") as directory:
        path = Path(directory)
        colors = path / "colors.bin"
        colors.write_bytes(
            b"".join(native_colors(module, p, s) for p, s in matrix)
        )
        binary = path / "renderer"
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "tests/fullscreen-cache-native.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run(
            [str(binary), str(colors)], check=True, capture_output=True
        )
        check_frames(module, matrix, result.stdout)


if __name__ == "__main__":
    main()
