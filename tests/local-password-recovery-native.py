#!/usr/bin/env python3
"""Execute the actual local recovery lambda with a deterministic RNG boundary."""

import argparse
import subprocess
import tempfile
import textwrap
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def recovery_body():
    """Extract the real recovery lambda independently of YAML nesting."""
    text = YAML.read_text().split("  - id: reset_web_password_local\n", 1)[1]
    text = text.split("- lambda: |-\n", 1)[1]
    text = text.split("- component.update:", 1)[0]
    return textwrap.dedent(text.rstrip())


PREFIX = '\n#include <cassert>\n#include <cstring>\n#include <string>\n#include "usp_web_password_recovery.h"\nbool web_password_reset_armed = false;\nbool web_password_reset_reveal = false;\nstd::string web_password_override = "existing-nondefault-password";\n#define id(name) name\n#define ESP_LOGI(...) ((void)0)\nnamespace esphome { namespace web_server_base {\nstruct Server {\n  std::string password;\n  unsigned calls = 0;\n  void set_auth_password(const char *value) { password = value; ++calls; }\n};\nServer server;\nServer *global_web_server_base = &server;\n}}\nvoid reset() {\n'

CHECKS = '\n}\nint main() {\n  auto &server = esphome::web_server_base::server;\n  reset();\n  assert(web_password_override == "existing-nondefault-password");\n  assert(server.calls == 0 && !web_password_reset_reveal && rng_calls == 0);\n  web_password_reset_armed = true;\n  reset();\n  assert(!web_password_reset_armed && web_password_reset_reveal);\n  assert(rng_calls == 1 && server.calls == 1);\n  assert(web_password_override.size() == 32);\n  assert(web_password_override == server.password);\n  const auto first = web_password_override;\n  reset();\n  assert(web_password_override == first && server.calls == 1);\n  web_password_reset_armed = true;\n  reset();\n  assert(server.calls == 2 && web_password_override != first);\n  const std::string alphabet = "0123456789abcdef";\n  for (char value : web_password_override) assert(alphabet.find(value) != std::string::npos);\n  assert(web_password_override != "admin");\n  return 0;\n}\n'


def program():
    """Compose actual recovery code with explicit test boundaries."""
    return PREFIX + recovery_body() + CHECKS


def run_native():
    """Compile and execute production code under address/undefined sanitizers."""
    with tempfile.TemporaryDirectory(
        prefix="usp-local-recovery-"
    ) as directory:
        folder = Path(directory)
        (folder / "esp_random.h").write_text(
            "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
            "inline unsigned rng_calls = 0;\n"
            "inline void esp_fill_random(void *ptr, size_t size) {\n"
            "  ++rng_calls; auto *bytes = static_cast<uint8_t *>(ptr);\n"
            "  for (size_t i = 0; i < size; ++i) bytes[i] = i + rng_calls;\n}\n"
        )
        source = folder / "recovery.cpp"
        source.write_text(program())
        binary = folder / "recovery"
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I",
                str(folder),
                "-I",
                str(ROOT / "firmware/include"),
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run([str(binary)], check=True, timeout=15)


def check_contract():
    """Require local-only invocation, hidden transient storage and safe logging."""
    text = YAML.read_text()
    assert "usp_qa_reset_web_password_136" not in text
    assert text.count("script.execute: reset_web_password_local") == 1
    assert 'std::string("admin")' not in recovery_body()
    assert "web_password_override" not in "\n".join(
        line for line in recovery_body().splitlines() if "ESP_LOG" in line
    )
    assert "delay: 60s\n" in text
    assert "lambda: id(web_password_reset_reveal) = false;" in text
    public = (
        ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    ).read_text()
    assert "web_password_reset_reveal" not in public
    assert "usp_web_password_recovery" not in public


def main():
    """Run entirely offline; no credentials or device API access is needed."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    check_contract()
    run_native()
    logger.info(
        "LOCAL_PASSWORD_RECOVERY=PASS actual lambda/generator ASAN UBSAN; RNG/HTTP doubles"
    )


if __name__ == "__main__":
    main()
