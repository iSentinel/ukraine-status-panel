#!/usr/bin/env python3
"""Check bounded Wi-Fi result staging with actual methods and fixed wire fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    """Copy one entire C++ method without changing its branches."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def framework_root(explicit):
    """Locate the pinned installed ESPHome package without importing its state."""
    if explicit:
        return explicit
    binary = os.environ.get("ESPHOME_BIN") or shutil.which("esphome")
    if binary:
        prefix = Path(binary).resolve().parent.parent
        paths = list(prefix.glob("lib/python*/site-packages/esphome"))
        if paths:
            return paths[0]
    raise FileNotFoundError("Set ESPHOME_BIN or --esphome-root")


def verify_callback_context(root):
    """Reject an upstream change that moves this callback outside the main loop."""
    folder = root / "components/wifi"
    idf = (folder / "wifi_component_esp_idf.cpp").read_text()
    common = (folder / "wifi_component.cpp").read_text()
    loop = method(idf, "bool WiFiComponent::wifi_loop_()")
    event = method(idf, "void WiFiComponent::wifi_process_event_(")
    assert (
        "event_queue_.pop()" in loop and "wifi_process_event_(data);" in loop
    )
    assert idf.count("wifi_process_event_(") == 2
    assert "this->notify_scan_results_listeners_();" in event
    assert "wifi_loop_()" in method(common, "void WiFiComponent::loop()")
    return hashlib.sha256(idf.encode()).hexdigest()


def prepare():
    """Read actual publication, collector and serializer code and declarations."""
    folder = ROOT / "components/usp_wifi_scan"
    source = (folder / "wifi_scan_api.cpp").read_text()
    header = (folder / "wifi_scan_api.h").read_text()
    names = (
        "void WifiScanApi::collect_results_(",
        "uint16_t WifiScanApi::deduplicate_(",
        "void WifiScanApi::sort_by_rssi_(",
        "bool WifiScanApi::append_(",
        "bool WifiScanApi::append_ssid_(",
        "bool WifiScanApi::build_json_(",
        "void WifiScanApi::on_wifi_scan_results(",
        "void WifiScanApi::loop()",
        "bool WifiScanApi::publish_results_()",
        "bool WifiScanApi::lock_(",
        "void WifiScanApi::unlock_()",
    )
    values = {
        "@PROPOSAL_METHODS@": "\n".join(
            method(source, name) for name in names
        ),
        "@ENTRY_DECL@": method(header, "struct WifiScanEntry") + ";",
        "@STATE_DECL@": method(header, "enum class WifiScanState") + ";",
        "@LOCK_DECL@": re.search(r"bool lock_\([^;]+;", header).group(),
        "@DEFER_DECL@": re.search(
            r"static constexpr uint32_t SCAN_DEFER_MS[^;]+;", header
        ).group(),
        "@LIMIT_DECLS@": "\n".join(
            re.findall(
                r"static constexpr size_t (?:MAX_NETWORKS|RESPONSE_BYTES)[^;]+;",
                header,
            )
        ),
    }
    content = (ROOT / "tests/wifi-scan-results-host.cpp.in").read_text()
    for key, value in values.items():
        content = content.replace(key, value)
    assert not re.search(r"@[A-Z_]+@", content)
    return content


def run_native(compiler, directory):
    """Capture synthetic JSON separately so fixture values do not fill the log."""
    source, binary = directory / "actual.cpp", directory / "actual"
    source.write_text(prepare())
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        str(source),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode:
        logger.error("{}", result.stderr[-2000:])
        raise RuntimeError("Native scan-results assertion failed")
    return result.stdout


def main():
    """Verify main-task ownership, bounded staging and unchanged wire payloads."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--esphome-root", type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    framework = verify_callback_context(framework_root(args.esphome_root))
    with tempfile.TemporaryDirectory(prefix="usp-wifi-results-") as directory:
        output = run_native(compiler, Path(directory))
    rows = [
        line.split("=", 1)[1]
        for line in output.splitlines()
        if line.startswith("BF12_JSON=")
    ]
    expected = json.loads(
        (ROOT / "tests/fixtures/wifi-scan-wire.json").read_text()
    )
    assert rows == expected["wire_json"], "The existing wire JSON changed"
    assert len(rows) == 4 and "BF12B_STAGING_NATIVE=PASS" in output
    logger.info(
        "WIFI_SCAN_RESULTS_NATIVE=PASS staging, wire4, main-task ownership"
    )
    logger.info("Pinned Wi-Fi event queue source SHA256={}", framework)


if __name__ == "__main__":
    main()
