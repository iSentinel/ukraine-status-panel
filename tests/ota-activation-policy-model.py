#!/usr/bin/env python3
"""Behavior model for fail-closed OTA activation eligibility policy."""
from dataclasses import dataclass


@dataclass
class Partition:
    address: int
    app: bool = True
    valid: bool = True


@dataclass
class Evidence:
    manifest: bool = True
    image: bool = True
    finalized: bool = True
    post_state_valid: bool = True
    descriptor: bool = True
    digest: bool = True
    post_failure_none: bool = True
    session: bool = True


def evaluate(target: Partition | None, running: Partition | None,
             ev: Evidence) -> str:
    if target is None or running is None or not target.valid:
        return "invalid_target"
    if not target.app:
        return "not_app_partition"
    if target.address == running.address:
        return "target_is_running"
    if not ev.manifest:
        return "manifest_not_verified"
    if not ev.image:
        return "image_not_verified"
    if not ev.finalized:
        return "write_not_finalized"
    if not (ev.post_state_valid and ev.descriptor and ev.digest and ev.post_failure_none):
        return "post_write_not_valid"
    if not ev.session:
        return "session_not_current"
    return "eligible"


def main() -> None:
    target = Partition(0x410000)
    running = Partition(0x10000)
    cases = [
        ("ok", evaluate(target, running, Evidence()), "eligible"),
        ("null", evaluate(None, running, Evidence()), "invalid_target"),
        ("invalid", evaluate(Partition(0x410000, valid=False), running, Evidence()), "invalid_target"),
        ("not_app", evaluate(Partition(0x410000, app=False), running, Evidence()), "not_app_partition"),
        ("running", evaluate(Partition(running.address), running, Evidence()), "target_is_running"),
        ("manifest", evaluate(target, running, Evidence(manifest=False)), "manifest_not_verified"),
        ("image", evaluate(target, running, Evidence(image=False)), "image_not_verified"),
        ("final", evaluate(target, running, Evidence(finalized=False)), "write_not_finalized"),
        ("post_state", evaluate(target, running, Evidence(post_state_valid=False)), "post_write_not_valid"),
        ("descriptor", evaluate(target, running, Evidence(descriptor=False)), "post_write_not_valid"),
        ("digest", evaluate(target, running, Evidence(digest=False)), "post_write_not_valid"),
        ("post_failure", evaluate(target, running, Evidence(post_failure_none=False)), "post_write_not_valid"),
        ("session", evaluate(target, running, Evidence(session=False)), "session_not_current"),
    ]
    failures = [(name, got, want) for name, got, want in cases if got != want]
    if failures:
        raise SystemExit(f"model failures: {failures}")
    print("OTA_ACTIVATION_POLICY_MODEL=PASS cases=13 fail_closed=1 evidence_chain=1 running_partition_guard=1 no_mutation=1")


if __name__ == "__main__":
    main()
