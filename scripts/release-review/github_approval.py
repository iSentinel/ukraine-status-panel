#!/usr/bin/env python3
"""Require a real, independent GitHub Environment approval before signing."""

import argparse
import json
import os
from pathlib import Path
from urllib.request import Request, urlopen

from loguru import logger

ENVIRONMENT = "firmware-ed25519-review"
REPOSITORY = "iSentinel/ukraine-status-panel"


def require(condition, explanation):
    """Fail safely if GitHub approval cannot be independently established."""
    if not condition:
        raise ValueError(explanation)


def get_approvals(token, repo, run_id):
    """Query GitHub's review history, not a user-controlled input field."""
    url = f"https://api.github.com/repos/{repo}/actions/runs/{run_id}/approvals"
    request = Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2026-03-10",
    })
    with urlopen(request, timeout=20) as response:
        require(response.status == 200, "Review history could not be read")
        return json.load(response)


def approved_reviewer(history, expected, initiator, rerun_actor):
    """Accept only an explicitly approved review by another named user."""
    require(isinstance(history, list), "Invalid GitHub review history")
    require(expected and expected.lower() != initiator.lower(),
            "The initiator cannot independently approve")
    require(expected.lower() != rerun_actor.lower(),
            "A rerun cannot self-approve")
    reviews = []
    for item in history:
        envs = item.get("environments", [])
        if any(env.get("name") == ENVIRONMENT for env in envs):
            reviews.append(item)
    require(reviews, "Protected environment has no review history")
    last = reviews[-1]
    user = last.get("user") or {}
    login = user.get("login", "")
    require(last.get("state", "").lower() == "approved",
            "The environment was not explicitly approved")
    require(login.lower() == expected.lower(),
            "An unexpected person approved this run")
    require(login.lower() not in {initiator.lower(), rerun_actor.lower()},
            "Review must be independent of the run initiator")
    require(login.lower() != "isentinel",
            "Repository owner cannot serve as the independent reviewer")
    return login


def run_checks(args):
    """Read protected settings and compare the actual GitHub review event."""
    repo = os.getenv("GITHUB_REPOSITORY", "")
    ref = os.getenv("GITHUB_REF", "")
    event = os.getenv("GITHUB_EVENT_NAME", "")
    require(repo == REPOSITORY and ref == "refs/heads/main" and
            event == "workflow_dispatch", "Untrusted workflow execution")
    token = os.getenv("GITHUB_TOKEN", "")
    reviewer = os.getenv("USP_REVIEWER_LOGIN", "")
    require(token and reviewer, "Missing protected GitHub review settings")
    history = get_approvals(token, repo, args.run_id)
    return approved_reviewer(history, reviewer, args.actor, args.rerun_actor)


def main():
    """Write the verified GitHub login for the separate signing operation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", required=True, type=int)
    parser.add_argument("--actor", required=True)
    parser.add_argument("--rerun-actor", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    login = run_checks(args)
    with args.output.open("x") as output:
        json.dump({"reviewer_id": login, "run_id": args.run_id,
                   "environment": ENVIRONMENT}, output)
        output.write("\n")
    logger.info("GITHUB_INDEPENDENT_APPROVAL=PASS reviewer={} run_id={}",
                login, args.run_id)


if __name__ == "__main__":
    main()
