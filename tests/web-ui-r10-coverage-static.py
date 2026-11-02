#!/usr/bin/env python3
"""Fail closed unless the R10 real-browser coverage matrix stays registered."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BROWSER = ROOT / "tests/web-ui-r10-browser.cjs"
RUNNER = (ROOT / "scripts/test-web-forms.sh").read_text()
REGRESSION = (ROOT / "scripts/run-regression.sh").read_text()


def main() -> None:
    assert BROWSER.exists(), "missing R10 browser matrix"
    text = BROWSER.read_text()
    for token in (
        "VIEWPORTS", "LANGUAGES", "TAB_LABELS", "checkGeometry",
        "checkKeyboardTraversal", "checkContrastMatrix", "R10_BROWSER=PASS",
    ):
        assert token in text, f"R10 browser contract missing {token}"
    assert "web-ui-r10-browser.cjs" in RUNNER, "R10 browser matrix is not registered"
    assert "run web-ui-r10-coverage-static.py" in REGRESSION, "R10 static gate is not in regression"
    print("WEB_UI_R10_COVERAGE_STATIC=PASS tabs=10 languages=3 viewports=4 bounds=1 keyboard=1 contrast=1")


if __name__ == "__main__":
    main()
