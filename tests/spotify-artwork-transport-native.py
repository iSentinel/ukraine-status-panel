#!/usr/bin/env python3
"""Check the real artwork response collector and IDF transport with SDK substitutes."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_spotify_auth"
SHIMS = ROOT / "tests/spotify_token_host/shims"


def execute(command):
    """Keep compiler and memory-check failures visible in the saved log."""
    result = subprocess.run(
        command, cwd=ROOT, capture_output=True, text=True, timeout=60
    )
    if result.returncode:
        logger.error("{}", result.stderr[-4000:])
        result.check_returncode()
    print(result.stdout, end="")


def compile_case(compiler, folder, name, sources, defines):
    """Compile production bytes directly and run one isolated contract."""
    binary = folder / name
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(COMP),
        "-I",
        str(SHIMS),
    ]
    command += ["-D" + item for item in defines]
    command += [str(ROOT / source) for source in sources]
    execute(command + ["-o", str(binary)])
    execute([str(binary)])


def main():
    """Check TLS admission, caller buffers and complete provider cooldowns."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-artwork-transport-") as name:
        folder = Path(name)
        compile_case(
            compiler,
            folder,
            "body",
            ["tests/spotify-artwork-body-native.cpp"],
            [],
        )
        sources = [
            "components/usp_spotify_auth/artwork_transport.cpp",
            "tests/spotify-artwork-transport-native.cpp",
        ]
        compile_case(
            compiler,
            folder,
            "transport",
            sources,
            ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1"],
        )
        compile_case(
            compiler,
            folder,
            "no-bundle",
            sources,
            ["CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=0", "HOST_EXPECT_NO_BUNDLE"],
        )
    logger.info(
        "SPOTIFY_ARTWORK_TRANSPORT_NATIVE=PASS fake_IDF no_provider no_device"
    )


if __name__ == "__main__":
    main()
