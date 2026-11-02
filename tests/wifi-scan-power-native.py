#!/usr/bin/env python3
"""Verify actual scan power ownership with deterministic HTTP interleaving."""
import argparse
from pathlib import Path
import re
import subprocess
from loguru import logger
import hashlib
import os
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    """Read a complete actual method while preserving all its branches."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def source():
    """Bind real proposal methods and lock/time declarations to host adapters."""
    proposal = ROOT / "components/usp_wifi_scan"
    cpp = (proposal / "wifi_scan_api.cpp").read_text()
    header = (proposal / "wifi_scan_api.h").read_text()
    names = (
        "void WifiScanApi::loop()",
        "bool WifiScanApi::start_scan_()",
        "void WifiScanApi::expire_scan_()",
        "bool WifiScanApi::lock_(",
        "void WifiScanApi::unlock_()",
        "bool WifiScanApi::request_high_performance_()",
        "void WifiScanApi::release_high_performance_()",
        "void WifiScanApi::start_radio_scan_()",
        "bool WifiScanApi::update_scan_lease_()",
        "void WifiScanApi::consume_radio_failure_()",
        "void WifiScanApi::expire_scan_locked_()",
        "bool WifiScanApi::publish_results_()",
    )
    actual = "\n".join(method(cpp, name) for name in names)
    actual = actual.replace(
        "esphome::usp_wifi_scan::WifiScanApi::", "WifiScanApi::"
    )
    content = (
        (ROOT / "tests/wifi-scan-power-host.cpp.in")
        .read_text()
        .replace("@METHODS@", actual)
    )
    return bind_header(content, header)


def bind_header(content, header):
    """Use the real record storage shape, count and timeout declarations."""
    content = content.replace(
        "@ENTRY_DECL@", method(header, "struct WifiScanEntry") + ";"
    )
    content = content.replace(
        "@LIMIT_DECL@",
        re.search(
            r"static constexpr size_t MAX_NETWORKS[^;]+;", header
        ).group(),
    )
    for token, pattern in (
        ("@LOCK@", r"bool lock_\([^;]+;"),
        ("@DEFER@", r"static constexpr uint32_t SCAN_DEFER_MS[^;]+;"),
        ("@TIMEOUT@", r"static constexpr uint32_t SCAN_TIMEOUT_MS[^;]+;"),
    ):
        content = content.replace(token, re.search(pattern, header).group())
    return content


def verify_framework(explicit):
    """Require the real framework to apply lease changes in its main loop."""
    if explicit:
        root = explicit
    else:
        binary = os.environ.get("ESPHOME_BIN") or shutil.which("esphome")
        if not binary:
            raise FileNotFoundError("Set ESPHOME_BIN or --esphome-root")
        prefix = Path(binary).resolve().parent.parent
        root = next(prefix.glob("lib/python*/site-packages/esphome"))
    path = root / "components/wifi/wifi_component.cpp"
    cpp = path.read_text()
    loop = method(cpp, "void WiFiComponent::loop()")
    acquire = method(cpp, "bool WiFiComponent::request_high_performance()")
    release = method(cpp, "bool WiFiComponent::release_high_performance()")
    assert "uxSemaphoreGetCount(this->high_performance_semaphore_)" in loop
    assert "wifi_apply_power_save_()" in loop
    assert "xSemaphoreGive(this->high_performance_semaphore_)" in acquire
    assert "App.wake_loop_threadsafe()" in acquire
    assert "xSemaphoreTake(this->high_performance_semaphore_, 0)" in release
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(compiler, folder, disabled):
    """Compile actual methods with sanitizers and optional runtime power support."""
    target, binary = folder / "actual.cpp", folder / "actual"
    target.write_text(source())
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
    ]
    if disabled:
        command.append("-DUSP_TEST_NO_RUNTIME_POWER_SAVE")
    subprocess.run(command + [str(target), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode:
        logger.error("{}", result.stderr[-2000:])
        raise RuntimeError("Scan power native assertion failed")
    logger.info("runtime_power={} {}", not disabled, result.stdout.strip())


def main():
    """Check lifetime and deferred failures without network or radio access."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--esphome-root", type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    fingerprint = verify_framework(args.esphome_root)
    with tempfile.TemporaryDirectory(prefix="usp-scan-power-") as directory:
        for disabled in (False, True):
            run(compiler, Path(directory), disabled)
    logger.info(
        "WIFI_SCAN_POWER_NATIVE=PASS actual12 runtime2 framework={}",
        fingerprint,
    )


if __name__ == "__main__":
    main()
