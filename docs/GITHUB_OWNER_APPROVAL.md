# Owner Approval (default): no second reviewer, AWS or Ed25519

Ukraine Status Panel 0.115.415-dev is already installed. Its previously
accepted rollback is 0.115.414-dev. Never retry the original .415 build or OTA.

This policy is for the owner's private project: a manual owner declaration
and the existing firmware evidence/rollback/QA requirements. It is NOT an
independent review and does NOT provide a cryptographic approval signature.

## GitHub procedure

Once the owner-approval workflow is merged into main:

1. Review the 24 original hardware evidence files on the firmware server.
   Preserve checks from the physical panel and UART. A workflow action cannot
   establish that hardware was examined; this is the owner's responsibility.
2. Open https://github.com/iSentinel/ukraine-status-panel/actions and choose
   "Owner firmware approval (manual, no Ed25519)".
3. Choose "Run workflow" on branch main.
4. Fill these four fields EXACTLY (no additional spaces):

       version:
       0.115.415-dev

       receipt_sha256:
       a2412a570f06aeaf6da0cfc50db1cfc6e15cc9288832d4514a21fbab5c1aec4e

       packet_sha256:
       ee07627db447d7cae14cd8ee62dfcd33b84fc4f6dfb81cb0367ffbff21b04fcf

       statement:
       I APPROVE 0.115.415-dev AS OWNER

5. GitHub checks that both the original actor and rerun actor are iSentinel,
   the branch is main, the manual event is genuine and all fields match.
6. After the job passes, download the only artifact:
   receipt-v2-owner-approval-0.115.415-dev.
   It contains receipt-v2-owner-approval.json with an explicit
   independently_signed=false marker. It includes no key or firmware image.

A regular push/tag/pull request never creates an owner approval. There are no
cloud-storage secrets, required reviewers or separate signing accounts.

## Bring approval back to your firmware host or container

Transfer the downloaded JSON to the project, without overwriting an existing
approval, and set its mode to owner-only (0600). The canonical destination is:

    ./.work-state/receipt-v2-owner-approval.json

Do not install an Ed25519 private key, do not manufacture an identity, and do
not claim this file is a signature. Its GitHub run page is the recorded online
owner-action evidence; the local JSON is an owner declaration only.

Run these commands **only on your private firmware host** containing the
`.415` receipt-v2 scripts. The public `.407` source checkout does not
contain these private production-acceptance tools.

After the file is in your firmware host or container, run READ-ONLY checks:

    cd /path/to/private/ukraine-status-panel
    python3 -B scripts/owner_release_readiness.py
    python3 -B scripts/receipt_v2_accept.py
    python3 -B scripts/receipt_v2_owner_payload.py
    sha256sum .work-state/receipt-v2-owner-approval.json

Owner approval alone still does NOT apply a production release. Only after a
separate operator decision with the exact owner approval SHA256, exact source
receipt SHA256, exact packet SHA256 and version should the existing crash-safe
six-file checkpoint transaction run. See the server-side guide:

    docs/RELEASE_APPROVAL_OWNER.md

The device remains installed .415, with accepted rollback .414, until that
separate checkpoint acceptance transaction actually commits and is audited.

## Optional local owner approval without GitHub

The server-side owner review tool also offers an explicit local declaration,
using the same original 24 proof SHA256 values and required typed sentence.
See docs/RELEASE_APPROVAL_OWNER.md. Do not record both a GitHub and local owner
declaration for the same release; a release has only one actual owner approval.

## Optional independent signing

The older independent Ed25519 mode is retained on the firmware server as
an explicitly selected alternate path, not the default. No key or separate
reviewer is necessary for this owner workflow.

A manual owner approval is suitable for an individually managed hobby device.
For regulated production or multi-operator deployments, enforce an external
reviewer and real cryptographic release signing instead.
