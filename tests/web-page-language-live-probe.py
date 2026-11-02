#!/usr/bin/env python3
"""Read the panel page language with GET-only Chromium traffic."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import subprocess

from loguru import logger
import yaml

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = {"English": "en", "Русский": "ru", "Українська": "uk"}


def arguments():
    """Require fresh device evidence and a new output file."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--address", default="192.0.2.208")
    parser.add_argument("--device-json", type=Path, required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--deny-storage", action="store_true")
    args = parser.parse_args()
    args.root = args.root.resolve()
    return args


def payload(args):
    """Keep authentication in memory and validate the read-only device snapshot."""
    assert not args.output.exists(), "Output already exists"
    device = json.loads(args.device_json.read_text())
    assert device["version"] == args.expected_version
    assert type(device["services"]) is int and device["services"] == 0
    language = LANGUAGES[device["settings"]["UI Language"]]
    address = str(ipaddress.IPv4Address(args.address))
    secrets = yaml.safe_load((args.root / "firmware/secrets.yaml").read_text())
    password = secrets["web_server_password"]
    assert isinstance(password, str) and password
    return {
        "root": str(args.root),
        "url": f"http://{address}/",
        "username": "admin",
        "password": password,
        "expected_lang": language,
        "deny_storage": args.deny_storage,
    }


def run_browser(args, config):
    """Send credentials through a pipe, never through arguments or saved files."""
    node = args.root.parent / ".qa-tools/node/bin/node"
    script = Path(__file__).with_name("web-page-language-live.cjs")
    environment = os.environ.copy()
    environment.pop("DEBUG", None)
    environment.pop("PWDEBUG", None)
    environment.setdefault(
        "PLAYWRIGHT_BROWSERS_PATH", str(args.root.parent / ".qa-tools/playwright")
    )
    result = subprocess.run(
        [str(node), str(script)],
        input=json.dumps(config),
        text=True,
        capture_output=True,
        cwd=args.root,
        env=environment,
        timeout=55,
    )
    if result.returncode:
        safe_error = result.stderr.replace(config["password"], "[redacted]")
        args.output.with_suffix(".stderr.log").write_text(safe_error)
        raise RuntimeError("Browser probe failed; see sanitized error evidence")
    report = json.loads(result.stdout)
    assert report["status"] == "PASS" and report["blocked_writes"] == 0
    return report


def main():
    """Save only public verification metadata, without credentials or UI values."""
    args = arguments()
    config = payload(args)
    report = run_browser(args, config)
    report["version"] = args.expected_version
    report["device_evidence_sha256"] = hashlib.sha256(
        args.device_json.read_bytes()
    ).hexdigest()
    with args.output.open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    logger.info(
        "LIVE_PAGE_LANGUAGE=PASS lang={} reads={}",
        report["page_language"],
        report["read_requests"],
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        logger.error("LIVE_PAGE_LANGUAGE=FAIL {}", type(error).__name__)
        raise SystemExit(1)
