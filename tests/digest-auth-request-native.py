#!/usr/bin/env python3
"""Test real Digest verification across repeated callbacks of one HTTP request."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
URI = "/usp/spotify/auth/begin"
PASSWORD = "synthetic-fixture"


def md5(value):
    """Create independent synthetic Digest headers using Python's hash library."""
    return hashlib.md5(value.encode()).hexdigest()


def signed_header(nonce, count):
    """Sign a local fixture request without any real panel credentials."""
    opaque = "b" * 32
    ha1 = md5(f"admin:Login Required:{PASSWORD}")
    ha2 = md5(f"POST:{URI}")
    response = md5(f"{ha1}:{nonce}:{count}:fixture:auth:{ha2}")
    values = dict(
        username="admin",
        realm="Login Required",
        nonce=nonce,
        opaque=opaque,
        uri=URI,
        qop="auth",
        nc=count,
        cnonce="fixture",
        response=response,
    )
    return "Digest " + ", ".join(f'{k}="{v}"' for k, v in values.items())


def prepare(folder):
    """Extract actual production functions; only platform calls are replaced."""
    source = (
        ROOT / "components/web_server_idf/web_server_idf.cpp"
    ).read_text()
    start = source.index("// Extract the value of a Digest auth parameter")
    end = source.index("\n}  // namespace\n#endif", start)
    block = source[start:end]
    (folder / "digest-production.inc").write_text(block)
    values = dict(URI=URI, PASSWORD=PASSWORD, OPAQUE="b" * 32)
    for suffix, nonce, count in (
        ("", "a" * 32, "00000001"),
        ("2", "a" * 32, "00000002"),
        ("3", "c" * 32, "00000001"),
        ("4", "d" * 32, "00000001"),
    ):
        values["NONCE" + suffix] = nonce
        values["HEADER" + suffix] = signed_header(nonce, count)
    includes = [
        f"constexpr char {k}[]={json.dumps(v)};" for k, v in values.items()
    ]
    (folder / "signed-fixtures.inc").write_text("\n".join(includes))
    return "verified_response" in block


def run(args):
    """Compile actual middleware and Digest code with ASAN and UBSAN."""
    with tempfile.TemporaryDirectory(prefix="usp-digest-request-") as temp:
        folder = Path(temp)
        fixed = prepare(folder)
        binary = folder / "request-test"
        command = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-pthread",
            f"-DUSP_DIGEST_HAS_REQUEST_STATE={int(fixed)}",
            "-I",
            str(ROOT / "tests/web_credentials_host"),
            "-I",
            str(ROOT / "components/web_server_base"),
            "-I",
            str(ROOT),
            "-I",
            str(folder),
            str(ROOT / "tests/digest_auth_request_host/main.cpp"),
            str(ROOT / "components/web_server_base/web_server_base.cpp"),
            "-Wl,-l:libcrypto.so.3",
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        mode = ["--expect-duplicate"] if args.expect_duplicate else []
        subprocess.run([str(binary), *mode], check=True, timeout=30)


def main():
    """Keep reproduction mode explicit and separate from acceptance."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect-duplicate", action="store_true")
    args = parser.parse_args()
    run(args)
    logger.info(
        "Digest request test complete; reproduction={}", args.expect_duplicate
    )


if __name__ == "__main__":
    main()
