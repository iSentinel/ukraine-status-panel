"""Verify OTA state ownership covers public mutable and HTTP entrypoints."""

import argparse
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
NAMES = (
    "reset_image_preflight",
    "begin_image_preflight",
    "add_image_chunk",
    "finish_image_preflight",
    "reset_candidate",
    "abort_candidate",
    "begin_candidate",
    "add_candidate_chunk",
    "finish_candidate",
    "candidate_snapshot",
    "reset_writer_dry_run",
    "abort_writer_dry_run",
    "begin_writer_dry_run",
    "add_writer_dry_run_chunk",
    "finish_writer_dry_run",
    "writer_snapshot",
    "handleBody",
    "handleRequest",
)


def body(source, name):
    """Extract the public entry body for a lock-before-access coverage check."""
    position = source.index("OtaGuard::" + name + "(")
    start = source.index("{", position)
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1 : end - 1]


def main():
    """Keep idle expiry and immutable readiness outside blocking UI waits."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text()
    header = (ROOT / "components/usp_ota_guard/ota_guard.h").read_text()
    for name in NAMES:
        assert (
            body(source, name)
            .lstrip()
            .startswith(
                "std::lock_guard<std::recursive_mutex> state_lock(this->state_mutex_);"
            )
        ), name
    loop = body(source, "loop")
    assert (
        "std::try_to_lock" in loop
        and "if (!state_lock.owns_lock()) return;" in loop
    )
    for expiry in ("session", "commit_arm", "write_permit"):
        assert "this->expire_" + expiry + "_if_needed_();" in loop
    assert "mutable std::recursive_mutex state_mutex_;" in header
    for flag in (
        "sodium_ready_",
        "self_test_ok_",
        "ready_",
        "preflight_route_registered_",
    ):
        assert "std::atomic<bool> " + flag + "{false};" in header
    for field in ("image_status", "image_bytes_seen", "candidate_state"):
        part = header.split(field + "() const {", 1)[1].split("}", 1)[0]
        assert "state_lock(this->state_mutex_)" in part, field
    logger.info(
        "OTA_GUARD_LOCK_COVERAGE=PASS entries={} nonblocking_loop atomic_flags",
        len(NAMES),
    )


if __name__ == "__main__":
    main()
