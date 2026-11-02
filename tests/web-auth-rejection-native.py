#!/usr/bin/env python3
"""Check real POST rejection code and multipart parsing without a device."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/web_auth_rejection_host"
FLAGS = [
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer",
    "-fno-pie",
]


def function_body(source, signature):
    """Extract a selected original function without changing its statements."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def bind_request_state(trial):
    """Execute real rejection fields, accessors and challenge admission code."""
    path = trial / "shims/esphome/components/web_server_idf/web_server_idf.h"
    text = path.read_text()
    header = (ROOT / "components/web_server_idf/web_server_idf.h").read_text()
    source = (
        ROOT / "components/web_server_idf/web_server_idf.cpp"
    ).read_text()
    field = re.search(r"mutable\s+bool\s+auth_rejected_[^;]*;", header).group()
    text = text.replace("mutable bool rejected{false};", field)
    for signature in (
        "bool auth_rejected() const",
        "void mark_auth_rejected() const",
    ):
        text = text.replace(
            function_body(text, signature), function_body(header, signature)
        )
    signature = "void AsyncWebServerRequest::requestAuthentication() const"
    prefix = function_body(source, signature).split("httpd_resp_set_hdr", 1)[0]
    prefix = prefix.replace("AsyncWebServerRequest::", "")
    prefix = prefix.replace(
        "requestAuthentication() const", "requestAuthentication()"
    )
    replacement = prefix + (
        "++this->challenges; fixture_responses.push_back(401);"
        " response_status=401;\n}"
    )
    text = text.replace(
        function_body(text, "void requestAuthentication()"), replacement
    )
    path.write_text(text)


def prepare(trial):
    """Replace platform I/O while preserving production request control."""
    shutil.copytree(FIXTURE / "shims", trial / "shims")
    bind_request_state(trial)
    shutil.copy2(FIXTURE / "main.cpp", trial / "main.cpp")
    source = (
        ROOT / "components/web_server_idf/web_server_idf.cpp"
    ).read_text()
    names = (
        "esp_err_t AsyncWebServer::handle_raw_body_",
        "esp_err_t AsyncWebServer::request_handler_(",
        "esp_err_t AsyncWebServer::handle_multipart_upload_",
    )
    parts = [function_body(source, name) for name in names]
    (trial / "combined-production.inc").write_text("\n".join(parts))
    source = (ROOT / "components/web_server_idf/utils.cpp").read_text()
    header = (ROOT / "components/web_server_idf/utils.h").read_text()
    parts = [function_body(header, "inline bool char_equals_ci")]
    for name in ("bool str_ncmp_ci", "const char *strcasestr_n"):
        parts.append(function_body(source, name))
    (trial / "string-utils.cpp").write_text(
        "#include <cctype>\n#include <cstring>\n"
        "namespace esphome::web_server_idf {\n" + "\n".join(parts) + "\n}\n"
    )


def compile_parser(trial, parser):
    """Keep the upstream disabled-log warning exception limited to its C file."""
    target = trial / "parser.o"
    subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-Wno-unused-parameter",
            *FLAGS,
            "-c",
            str(parser / "multipart_parser.c"),
            "-o",
            str(target),
        ],
        check=True,
        timeout=60,
    )
    return target


def compile_test(trial, parser):
    """Compile actual receivers, middleware and multipart reader with checks."""
    parser_object = compile_parser(trial, parser)
    command = [
        "g++",
        "-std=c++20",
        *FLAGS,
        "-no-pie",
        "-pthread",
        "-DUSE_WEBSERVER_OTA=1",
    ]
    for path in (
        trial / "shims",
        ROOT / "components/web_server_base",
        ROOT / "components/web_server_idf",
        parser,
        trial,
    ):
        command += ["-I", str(path)]
    command += [
        str(trial / "main.cpp"),
        str(trial / "string-utils.cpp"),
        str(ROOT / "components/web_server_base/web_server_base.cpp"),
        str(ROOT / "components/web_server_idf/multipart.cpp"),
        str(parser_object),
        "-o",
        str(trial / "probe"),
    ]
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(trial / "probe")], check=True, timeout=15)


def main():
    """Run isolated synthetic requests; never contact a network or flash."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--parser-root", type=Path, default=FIXTURE / "vendor/multipart-parser"
    )
    args = parser.parse_args()
    for name in ("multipart_parser.c", "multipart_parser.h"):
        if not (args.parser_root / name).is_file():
            parser.error(f"Missing parser dependency: {name}")
    with tempfile.TemporaryDirectory(prefix="usp-auth-rejection-") as temp:
        trial = Path(temp)
        prepare(trial)
        compile_test(trial, args.parser_root)
    checksum = hashlib.sha256(
        (args.parser_root / "multipart_parser.c").read_bytes()
    ).hexdigest()
    logger.info(
        "WEB_AUTH_REJECTION_NATIVE=PASS cases16 parser_sha256={} no_device",
        checksum,
    )


if __name__ == "__main__":
    main()
