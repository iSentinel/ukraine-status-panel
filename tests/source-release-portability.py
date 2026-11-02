#!/usr/bin/env python3
"""Verify source/security checks also work outside a Git checkout."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(root: Path, script: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(root / "scripts" / script)],
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )


def make_tree(root: Path) -> None:
    for directory in ("firmware", "components", "assets", "include", "tests", "scripts"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    (root / "firmware" / "ukraine-status-panel.yaml").write_text("esphome:\n  name: archive-test\n")
    shutil.copy2(ROOT / "scripts" / "check-source.sh", root / "scripts" / "check-source.sh")
    shutil.copy2(ROOT / "scripts" / "secret-scan.sh", root / "scripts" / "secret-scan.sh")


def require_pass(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise AssertionError(f"{label} failed: {result.stderr or result.stdout}")
    if "fatal: not a git repository" in result.stderr:
        raise AssertionError(f"{label} emitted Git repository fatal error")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="usp-source-portability-") as temp:
        root = Path(temp)
        make_tree(root)
        require_pass(run(root, "check-source.sh"), "archive check-source")
        require_pass(run(root, "secret-scan.sh"), "archive secret-scan")

        (root / ".env").write_text("PRIVATE=1\n")
        if run(root, "secret-scan.sh").returncode == 0:
            raise AssertionError("archive secret scan accepted .env")
        (root / ".env").unlink()

        private_key_marker = "-----BEGIN OPENSSH " + "PRIVATE KEY-----"
        (root / "leak.txt").write_text(
            private_key_marker + "\nnot-a-real-key\n"
        )
        if run(root, "secret-scan.sh").returncode == 0:
            raise AssertionError("archive secret scan accepted private-key marker")
        (root / "leak.txt").write_text(
            "api_encryption_key: " + "A" * 44 + "\n"
        )
        if run(root, "secret-scan.sh").returncode == 0:
            raise AssertionError("archive secret scan accepted Native API key")

    print("SOURCE_RELEASE_PORTABILITY=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
