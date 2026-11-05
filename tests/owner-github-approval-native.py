#!/usr/bin/env python3
"""Test that GitHub owner confirmation refuses forged or incomplete inputs."""

import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "scripts/owner-github-approval.py"
SPEC = importlib.util.spec_from_file_location("usp_owner_github", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def environment(**changes):
    """Create a simulated manual GitHub owner action for local-only tests."""
    result = {
        "GITHUB_REPOSITORY": MODULE.REPOSITORY,
        "GITHUB_REF": "refs/heads/main",
        "GITHUB_EVENT_NAME": "workflow_dispatch",
        "GITHUB_ACTOR": MODULE.OWNER,
        "GITHUB_TRIGGERING_ACTOR": MODULE.OWNER,
    }
    result.update(changes)
    return result


def options(**changes):
    """Create an original release authorization request for fixture tests."""
    data = {
        "version": MODULE.VERSION,
        "receipt_sha256": MODULE.DRAFT_SHA256,
        "packet_sha256": MODULE.PACKET_SHA256,
        "statement": MODULE.STATEMENT,
    }
    data.update(changes)
    return SimpleNamespace(**data)


class GithubOwnerApprovalTests(unittest.TestCase):
    """Do not write a real owner record or contact physical devices."""

    def test_good_owner_has_unsigned_receipt(self):
        """A manual owner can create only a clearly unsigned record."""
        result = MODULE.approved_record(options(), environment())
        self.assertEqual(result["release_version"], MODULE.VERSION)
        self.assertFalse(result["independently_signed"])
        self.assertEqual(result["reviewed_proof_files"], 24)

    def test_wrong_owner(self):
        """A second GitHub identity cannot act as owner."""
        with self.assertRaisesRegex(ValueError, "repository owner"):
            MODULE.approved_record(options(), environment(GITHUB_ACTOR="other"))

    def test_wrong_rerun_actor(self):
        """A third party cannot reuse an owner-authorized run."""
        env = environment(GITHUB_TRIGGERING_ACTOR="other")
        with self.assertRaisesRegex(ValueError, "also initiate"):
            MODULE.approved_record(options(), env)

    def test_push_never_auto_approves(self):
        """Approval must never be triggered by a push."""
        env = environment(GITHUB_EVENT_NAME="push")
        with self.assertRaisesRegex(ValueError, "started manually"):
            MODULE.approved_record(options(), env)

    def test_other_branch_refused(self):
        """A development branch cannot authorize accepted firmware."""
        env = environment(GITHUB_REF="refs/heads/testing")
        with self.assertRaisesRegex(ValueError, "restricted to main"):
            MODULE.approved_record(options(), env)

    def test_other_repository_refused(self):
        """Forked workflow execution never authorizes original firmware."""
        env = environment(GITHUB_REPOSITORY="any/other")
        with self.assertRaisesRegex(ValueError, "Unexpected repository"):
            MODULE.approved_record(options(), env)

    def test_wrong_receipt_sha(self):
        """Refuse a copied or changed proof draft."""
        with self.assertRaisesRegex(ValueError, "Original receipt"):
            MODULE.approved_record(options(receipt_sha256="0" * 64), environment())

    def test_wrong_packet_sha(self):
        """Refuse a changed proof archive."""
        with self.assertRaisesRegex(ValueError, "Original packet"):
            MODULE.approved_record(options(packet_sha256="0" * 64), environment())

    def test_missing_statement(self):
        """A typed declaration is required rather than inferred consent."""
        with self.assertRaisesRegex(ValueError, "Explicit owner consent"):
            MODULE.approved_record(options(statement=""), environment())

    def test_wrong_release(self):
        """The frozen .415 hardware evidence cannot approve another version."""
        with self.assertRaisesRegex(ValueError, "Unexpected release"):
            MODULE.approved_record(options(version="0.115.416-dev"), environment())


if __name__ == "__main__":
    unittest.main(verbosity=2)
