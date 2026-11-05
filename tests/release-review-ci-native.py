#!/usr/bin/env python3
"""Exercise review packet integrity, GitHub approval and Ed25519 signing."""

import base64
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import sys
import tarfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/release-review"))
import github_approval as review  # noqa: E402
import sign_approval as signer  # noqa: E402
import verify_packet as packet  # noqa: E402


def draft_data():
    """Make a synthetic 24-proof draft, never real release evidence."""
    evidence = {f"proof-{i:02}.txt": f"proof {i}".encode() for i in range(24)}
    body = {
        "schema": "usp-hardware-readonly-proof-v2-draft",
        "status": "PRESEAL_DRAFT_NOT_ACCEPTED",
        "release_version": packet.VERSION,
        "proof_sha256": {name: sha256(value).hexdigest()
                         for name, value in evidence.items()},
    }
    return json.dumps(body, sort_keys=True).encode(), evidence


def make_tar(draft, evidence, extra=None, duplicate=False, link=False):
    """Package artificial original proofs with explicit unsafe-case switches."""
    items = {name: b"test metadata" for name in packet.METADATA}
    items["reconciliation-receipt-draft.json"] = draft
    items.update({f"evidence/{name}": value
                  for name, value in evidence.items()})
    if extra:
        items.update(extra)
    stream = BytesIO()
    with tarfile.open(fileobj=stream, mode="w:gz") as archive:
        for name, blob in sorted(items.items()):
            entry = tarfile.TarInfo(packet.ROOT + name)
            entry.size = len(blob)
            if link and name.endswith("proof-00.txt"):
                entry.type = tarfile.SYMTYPE
                entry.linkname = "/etc/passwd"
                entry.size = 0
            archive.addfile(entry, BytesIO(blob))
        if duplicate:
            name = packet.ROOT + "README.md"
            entry = tarfile.TarInfo(name)
            entry.size = 1
            archive.addfile(entry, BytesIO(b"X"))
    return stream.getvalue()


def fake_history(login="independent-user", state="approved", name=None):
    """Return a minimal model of GitHub's own environment review event."""
    return [{"state": state, "user": {"login": login},
             "environments": [{"name": name or review.ENVIRONMENT}]}]


class PacketTests(unittest.TestCase):
    """Verify the full review packet contract using synthetic data."""

    def setUp(self):
        """Build independent fixture content with valid test hashes."""
        self.draft, self.evidence = draft_data()
        self.blob = make_tar(self.draft, self.evidence)

    def verify(self, blob=None):
        """Temporarily substitute only local synthetic fixture hashes."""
        value = self.blob if blob is None else blob
        with patch.object(packet, "PACKET_SHA256", sha256(value).hexdigest()):
            with patch.object(packet, "DRAFT_SHA256", sha256(self.draft).hexdigest()):
                return packet.verify_packet(value)

    def test_all_24_evidence_files(self):
        """Accept only intact metadata plus all 24 original evidence blobs."""
        self.assertEqual(self.verify(), self.draft)

    def test_wrong_packet_hash(self):
        """Reject valid archives without the correct pinned identity."""
        with self.assertRaisesRegex(ValueError, "Packet SHA256"):
            packet.verify_packet(self.blob)

    def test_modified_proof_content(self):
        """Reject changed proof data despite a newly matching packet hash."""
        bad = dict(self.evidence)
        bad["proof-07.txt"] = b"changed without receipt update"
        with self.assertRaisesRegex(ValueError, "Changed original evidence"):
            self.verify(make_tar(self.draft, bad))

    def test_missing_evidence(self):
        """Reject a missing original proof before any signature."""
        bad = dict(self.evidence)
        del bad["proof-02.txt"]
        with self.assertRaisesRegex(ValueError, "29 review files"):
            self.verify(make_tar(self.draft, bad))

    def test_duplicate_evidence(self):
        """Reject duplicate archive metadata entries."""
        with self.assertRaisesRegex(ValueError, "29 review files"):
            self.verify(make_tar(self.draft, self.evidence, duplicate=True))

    def test_symbolic_link(self):
        """Reject archive links without following them."""
        with self.assertRaisesRegex(ValueError, "non-file"):
            self.verify(make_tar(self.draft, self.evidence, link=True))

    def test_wrong_draft_even_if_packet_sha_matches(self):
        """Require the original frozen receipt SHA256."""
        wrong = self.draft + b" "
        with self.assertRaisesRegex(ValueError, "receipt hash differs"):
            self.verify(make_tar(wrong, self.evidence))

    def test_http_input_refused(self):
        """Never fetch reviewer evidence over HTTP."""
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            packet.download_packet("http://example.com/file.tar.gz")


class ApprovalTests(unittest.TestCase):
    """Require independently attributable GitHub deployment approvals."""

    def test_real_reviewer(self):
        """Accept an explicit review from a different configured user."""
        self.assertEqual(review.approved_reviewer(
            fake_history(), "independent-user", "iSentinel", "iSentinel"),
            "independent-user")

    def test_no_review(self):
        """An unprotected environment never grants signing."""
        with self.assertRaisesRegex(ValueError, "no review history"):
            review.approved_reviewer([], "independent-user", "owner", "owner")

    def test_rejected_review(self):
        """Reject a review not explicitly approved."""
        with self.assertRaisesRegex(ValueError, "not explicitly approved"):
            review.approved_reviewer(
                fake_history(state="rejected"), "independent-user", "owner", "owner")

    def test_wrong_reviewer(self):
        """Reject an approval from a different account."""
        with self.assertRaisesRegex(ValueError, "unexpected person"):
            review.approved_reviewer(
                fake_history(login="attacker"), "independent-user", "owner", "owner")

    def test_self_review(self):
        """Do not allow the initiator to approve their own run."""
        with self.assertRaisesRegex(ValueError, "initiator cannot"):
            review.approved_reviewer(
                fake_history(login="owner"), "owner", "owner", "owner")

    def test_rerun_self_review(self):
        """Reject self-review by a user who reran the job."""
        with self.assertRaisesRegex(ValueError, "rerun cannot"):
            review.approved_reviewer(
                fake_history(), "independent-user", "owner", "independent-user")

    def test_repository_owner_rejected(self):
        """Require a different identity than the repository owner."""
        with self.assertRaisesRegex(ValueError, "Repository owner"):
            review.approved_reviewer(
                fake_history(login="iSentinel"), "iSentinel", "developer", "developer")


class SignerTests(unittest.TestCase):
    """Verify a synthetic signature with the exact Ed25519 public key."""

    def setUp(self):
        """Use a new in-memory test key, never a production key."""
        self.key = Ed25519PrivateKey.generate()
        self.draft, _ = draft_data()
        self.der = self.key.public_key().public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo)

    def test_signature_and_review_contract(self):
        """Verify the canonical fields required by the V2 production verifier."""
        digest = sha256(self.draft).hexdigest()
        with patch.object(packet, "DRAFT_SHA256", digest):
            with patch.object(signer, "DRAFT_SHA256", digest):
                approval = signer.approve(self.draft, "independent-user", self.key)
        fields = {key: approval[key] for key in signer.FIELDS}
        payload = json.dumps(fields, sort_keys=True, separators=(",", ":")).encode()
        signature = base64.b64decode(approval["signature_b64"])
        self.key.public_key().verify(signature, payload)
        self.assertEqual(approval["receipt_sha256"], digest)

    def test_wrong_trusted_public_key(self):
        """Refuse a key that differs from out-of-band trusted fingerprint."""
        pem = self.key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
        encoded = base64.b64encode(pem).decode()
        with self.assertRaisesRegex(ValueError, "key identity differs"):
            signer.signing_key(encoded, "0" * 64)
        self.assertIsInstance(
            signer.signing_key(encoded, sha256(self.der).hexdigest()),
            Ed25519PrivateKey)


if __name__ == "__main__":
    unittest.main(verbosity=2)
