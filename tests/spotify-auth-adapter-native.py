#!/usr/bin/env python3
"""Check actual private HTTP entrypoints and form parsing with SDK substitutes."""
import argparse
from pathlib import Path
import runpy
import shutil
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def execute(compiler, folder, adapter, authenticated):
    """Reuse the worker's pinned dependencies instead of a separate build recipe."""
    worker = runpy.run_path(str(ROOT / "tests/spotify-token-worker-native.py"))
    suffix = ("auth" if authenticated else "noauth") if adapter else "form"
    command = worker["compiler_flags"]()
    command[0] = compiler
    if adapter:
        component = ROOT / "components/usp_spotify_auth"
        names = (
            "spotify_auth.cpp",
            "control_transport.cpp",
            "stage4_transport.cpp",
            "token_transport.cpp",
            "playback_transport.cpp",
            "artwork_transport.cpp",
            "artwork_decoder.cpp",
        )
        command += [str(component / name) for name in names]
        command += [
            str(worker["decoder_object"](folder)),
            "-DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1",
        ]
        if authenticated:
            command.append("-DUSE_WEBSERVER_AUTH")
    name = (
        "spotify-auth-adapter-native.cpp"
        if adapter
        else "spotify-auth-form-native.cpp"
    )
    binary = folder / suffix
    command += [str(ROOT / "tests" / name), "-o", str(binary)]
    worker["checked_run"](command, 60)
    worker["checked_run"]([str(binary)], 30)


def main():
    """Keep registration and body/state tests independent of real provider access."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-auth-adapter-") as name:
        folder = Path(name)
        for adapter, authenticated in (
            (False, False),
            (True, True),
            (True, False),
        ):
            execute(compiler, folder, adapter, authenticated)
    logger.info(
        "SPOTIFY_AUTH_ADAPTER_NATIVE_RUN=PASS actual_entrypoints no_provider/device"
    )


if __name__ == "__main__":
    main()
