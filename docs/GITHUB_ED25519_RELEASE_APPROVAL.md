# Independent Ed25519 release approval (GitHub Actions)

This procedure applies to Ukraine Status Panel 0.115.415-dev. Its accepted
rollback is 0.115.414-dev; the original Build1/OTA1 allowance is already
consumed (1/1). Neither Actions workflow builds, flashes or accepts firmware.

## Security model

- The safety-tests workflow uses no secrets and rejects bad proof/approval data.
- The signer workflow is manually dispatched from the trusted main branch.
- The signing job uses the protected Environment firmware-ed25519-review and
  runs on a separate GitHub-hosted runner, never Proxmox/LXC 106.
- GitHub's run-approval API is checked to establish that the actual reviewer
  approved the environment and is NOT the initiator, rerun actor or iSentinel.
- Both the original 17 MB evidence packet and the signing private key are
  Environment secrets (URL and base64 key). Neither is published as an artifact.
- The signed JSON output is the existing receipt-v2 Ed25519 approval contract.
  A successful signature alone does NOT apply the production checkpoint.
- The packet SHA256, original draft SHA256 and each of the 24 original proof
  hashes are verified. Files from the archive are never executed.
- The signing key is refused unless its DER public-key SHA256 matches the
  independently verified fingerprint set in the protected Environment.

## Frozen evidence for this exact .415 release

    Packet on LXC 106:
    /root/config/ukraine-status-panel/artifacts/release-reconciliation-20261105-r1/independent-review-kit-20261105.tar.gz

    Packet SHA256:
    ee07627db447d7cae14cd8ee62dfcd33b84fc4f6dfb81cb0367ffbff21b04fcf

    Original draft SHA256:
    a2412a570f06aeaf6da0cfc50db1cfc6e15cc9288832d4514a21fbab5c1aec4e

An independent human must examine the physical LCD/UART evidence, rollback
history and all 24 proof hashes before deciding to approve. Hash validation
is not proof that a physical observation really happened.

Provide the exact original packet at a private, read-only HTTPS URL reachable
by GitHub-hosted runners, ideally a suitably long-lived pre-signed URL.
Never upload the original packet (including firmware images and UART evidence)
as a PUBLIC GitHub Release asset. GitHub-hosted runners cannot reach LXC 106's
private LAN directly.

## One-time GitHub Environment setup

Open https://github.com/iSentinel/ukraine-status-panel/settings/environments
and create an Environment named firmware-ed25519-review BEFORE storing a key.

Configure ALL of these protection rules:

1. Required reviewers: one real independent GitHub login with repository read
   access, DIFFERENT from iSentinel and from the person running the workflow.
2. Enable Prevent self-review.
3. Disable administrator bypass of protection rules.
4. Restrict deployment branches to main only.

This repository is public, so GitHub supports required Environment reviewers
on Free plans. An unprotected auto-created Environment is not sufficient:
the signer additionally checks GitHub's actual approval history and fails
without a legitimate review. The available GitHub integration does not expose
Environment administration or secret-creation actions, so the owner must
configure these controls in GitHub Settings.

On the reviewer's separate workstation, generate the real Ed25519 key:

    umask 077
    openssl genpkey -algorithm ED25519 -out reviewer-private.pem
    openssl pkey -in reviewer-private.pem -pubout -out reviewer-public.pem
    openssl pkey -pubin -in reviewer-public.pem -outform DER | sha256sum

The last SHA256 is the DER public-key fingerprint. Validate that fingerprint
and the public PEM with the Proxmox administrator over a DIFFERENT trusted
channel. The private key must never be generated or stored on Proxmox.

Set exactly these protected Environment values:

    Secret USP_REVIEW_SIGNING_KEY_B64   Base64 of reviewer-private.pem
    Secret USP_REVIEW_PACKET_URL        Private HTTPS URL of the exact packet
    Variable USP_REVIEWER_LOGIN         Actual independent GitHub login
    Variable USP_REVIEW_PUBLIC_KEY_SHA256  Verified DER public-key SHA256

To set the key securely from a trusted reviewer computer with GitHub CLI:

    base64 -w0 reviewer-private.pem | gh secret set USP_REVIEW_SIGNING_KEY_B64 \
      --env firmware-ed25519-review --repo iSentinel/ukraine-status-panel

Do not put the signing key into a repository-wide secret, PR, action input,
public log, packet, or commit.

## Run and approve

After merging the reviewed automation PR to protected main, open:
https://github.com/iSentinel/ukraine-status-panel/actions

1. Select Independent firmware receipt Ed25519 approval.
2. Click Run workflow with branch main.
3. Secret-free unit tests run first.
4. The independent reviewer checks the packet against the pinned SHA256
   hashes and uses Review deployments -> Approve and deploy.
5. The protected runner verifies the actual GitHub approval event, downloads
   the original packet via protected URL, hashes its 24 files and signs.
6. Download artifact receipt-v2-signed-approval-0.115.415-dev (30-day expiry).

Example authorized workstation download:

    gh run download GITHUB_RUN_ID --repo iSentinel/ukraine-status-panel \
      --name receipt-v2-signed-approval-0.115.415-dev \
      --dir ./signed-approval
    sha256sum ./signed-approval/receipt-v2-signed-approval.json

The artifact contains only an Ed25519-signed approval JSON; never import a
public key solely because it came alongside a signed file.

## Verify independently on LXC 106

An administrator must separately install the externally authenticated
reviewer's PEM public key at:

    /etc/ukraine-status-panel/receipt-v2-review-authority.json

Use the schema usp-receipt-v2-trust-v1, public_key_pem, and
authorized_reviewers containing the actual GitHub reviewer login. File and
parent directory must be root-owned, not symlinks and not writable by
group/others. The private key must stay OFF this server.

Transfer ONLY the signed JSON to:

    /root/config/ukraine-status-panel/.work-state/receipt-v2-signed-approval.json

Do not overwrite an existing approval. On LXC 106:

    cd /root/config/ukraine-status-panel
    /root/config/.qa-venv/bin/python3 -B scripts/receipt_v2_review.py
    /root/config/.qa-venv/bin/python3 -B scripts/receipt_v2_production.py
    /root/config/.qa-venv/bin/python3 -B scripts/audit-progress-state.py

These commands do not accept/promote the release. A SEPARATE operator must
authorize the exact reviewed approval and original receipt SHA256 before the
existing six-file signed-only transaction may be run with
scripts/receipt_v2_production.py --apply. Follow the complete, tested
transaction/recovery guide docs/RELEASE_V2_SIGNED_ACCEPTANCE.md.

The GitHub workflow has no server SSH credentials and cannot apply a V2
checkpoint. Recovery is an explicit, separately authorized operation.

## Tests and future releases

    python3 tests/release-review-ci-native.py
    python3 scripts/release-review/verify_packet.py --help
    python3 scripts/release-review/github_approval.py --help
    python3 scripts/release-review/sign_approval.py --help

The current workflow pins the actual .415 packet and receipt hashes. To use
this architecture for subsequent versions, create a newly reviewed packet,
update immutable release pins in a reviewed PR, and repeat the independent
approval. Never change expected hashes just to bypass a failed review.
